//TEST(compute):COMPARE_COMPUTE_EX:-slang -compute
struct Context
{
    int genFunc<TGenType>(TGenType t)
    {
        int i = 0;
        while (true)
        {
            i++;
            if (i > 5) break;
        }
        return i;
    }
};
//TEST_INPUT:ubuffer(data=[0 0 0 0], stride=4):dxbinding(0),glbinding(0),out
RWStructuredBuffer<int> outputBuffer;

[numthreads(4, 1, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Context c;
    outputBuffer[dispatchThreadID.x] = c.genFunc<int>(5);
}