//TEST(compute):COMPARE_COMPUTE(filecheck-buffer=CHECK):-vk -render-feature cooperative-vector -output-using-type -emit-spirv-directly
//TEST(compute):COMPARE_COMPUTE(filecheck-buffer=CHECK):-dx12 -render-feature cooperative-vector -dx12-experimental -use-dxil -output-using-type -profile cs_6_9 -Xslang... -Xdxc -Vd -X.
//TEST(compute):COMPARE_COMPUTE(filecheck-buffer=CHECK):-cpu -output-using-type

// CHECK: type: float
// CHECK-NEXT: 0.785398
// CHECK-NEXT: 1.107149
// CHECK-NEXT: 1.249046
// CHECK-NEXT: 1.325818

//TEST_INPUT:ubuffer(data=[0 0 0 0], stride=4):out,name=outputBuffer
RWStructuredBuffer<float> outputBuffer;

//TEST_INPUT:ubuffer(data=[1.0 2.0 3.0 4.0], stride=4),name=input
ByteAddressBuffer input;

[numthreads(1, 1, 1)]
void computeMain()
{
    CoopVec<float, 4> vec = coopVecLoad<4, float>(input);

    CoopVec<float, 4> result = atan(vec);

    for(int i = 0; i < result.getCount(); ++i)
        outputBuffer[i] = result[i];
}
