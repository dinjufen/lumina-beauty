# Replace MediaPipe's custom TFLite op Convolution2DTransposeBias (which tf2onnx passes through
# untranslated) with the equivalent standard ONNX ConvTranspose, then check the result on an image.
import sys
import numpy as np
import onnx
from onnx import helper, numpy_helper

src, dst = sys.argv[1], sys.argv[2]
m = onnx.load(src)
g = m.graph
init = {i.name: i for i in g.initializer}

idx = next(i for i, n in enumerate(g.node) if n.op_type == "TFL_Convolution2DTransposeBias")
node = g.node[idx]
x_nhwc, w_name, b_name = node.input
out_name = node.output[0]

# The node is fed through a NCHW->NHWC Transpose; take the NCHW tensor before it.
pre = next(n for n in g.node if x_nhwc in n.output)
assert pre.op_type == "Transpose" and list(helper.get_attribute_value(pre.attribute[0])) == [0, 2, 3, 1]
x_nchw = pre.input[0]

w = numpy_helper.to_array(init[w_name])            # TFLite layout: [out, kh, kw, in]
assert w.shape[1] == w.shape[2] == 2
w_onnx = np.ascontiguousarray(w.transpose(3, 0, 1, 2))  # ONNX ConvTranspose layout: [in, out, kh, kw]
g.initializer.append(numpy_helper.from_array(w_onnx, name=w_name + "_convtranspose"))

conv = helper.make_node("ConvTranspose", [x_nchw, w_name + "_convtranspose", b_name], [out_name + "_nchw"],
                        name="xn_conv2d_transpose", kernel_shape=[2, 2], strides=[2, 2])
back = helper.make_node("Transpose", [out_name + "_nchw"], [out_name], name="xn_conv2d_transpose_nhwc",
                        perm=[0, 2, 3, 1])
del g.node[idx]
g.node.insert(idx, back)
g.node.insert(idx, conv)

# Drop the now-unused Transpose if nothing else reads it.
if not any(x_nhwc in n.input for n in g.node):
    g.node.remove(pre)

onnx.checker.check_model(m)
onnx.save(m, dst)
print("saved", dst, "ops:", sorted({n.op_type for n in g.node}))

if len(sys.argv) > 3:
    import onnxruntime as ort, time
    from PIL import Image
    s = ort.InferenceSession(dst, providers=["CPUExecutionProvider"])
    img = Image.open(sys.argv[3]).convert("RGB").resize((256, 144), Image.BILINEAR)
    x = (np.asarray(img).astype(np.float32) / 255.0)[None]
    y = s.run(None, {s.get_inputs()[0].name: x})[0]
    t = time.perf_counter()
    for _ in range(50):
        s.run(None, {s.get_inputs()[0].name: x})
    print("output", y.shape, "min %.3f max %.3f mean %.3f" % (y.min(), y.max(), y.mean()),
          "%.2f ms/run" % ((time.perf_counter() - t) * 20))
    mask = (np.clip(y[0, :, :, 0], 0, 1) * 255).astype(np.uint8)
    sheet = Image.new("RGB", (512, 144))
    sheet.paste(img, (0, 0))
    sheet.paste(Image.fromarray(mask).convert("RGB"), (256, 0))
    sheet.resize((1024, 288), Image.NEAREST).save(sys.argv[4])
    print("wrote", sys.argv[4])
