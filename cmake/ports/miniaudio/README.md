# Pinned miniaudio initialization cleanup

Keep 0.11.25 and its upstream source checksum. The local patch frees CoreAudio
stream descriptions if the following sample-rate allocation/query fails. Once
backend device creation succeeds, mark the device initialized so subsequent
converter/fixed-callback allocation failures execute ordinary device teardown.
This prevents tracked-device/converter allocations and backend objects surviving
failed initialization; normal teardown remains authoritative, with no allocator
sweep or lowered test assertions. Actual bounded native initialization tests
exercise failure cleanup; Windows/Linux branches are supported but not locally run.

固定 0.11.25 及上游源校验值。补丁在采样率查询失败时释放 CoreAudio 格式描述；后端
设备创建成功后进入已初始化状态，使后续转换器/固定回调内存失败执行正常设备析构。
原生有限预算测试保持真实释放断言，不以分配器清扫掩盖生命周期错误。
