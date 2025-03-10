// PHZ
// 2020-11-20

#ifndef GDI_SCREEN_CAPTURE_H
#define GDI_SCREEN_CAPTURE_H

/*
功能：基于 FFmpeg 的 gdigrab 设备实现 Windows 屏幕捕获，支持多显示器选择，通过后台线程持续捕获图像。
​核心流程：初始化设备 → 启动线程捕获 → 解码数据 → 前端获取帧。
​应用场景：适用于实时屏幕录制、远程桌面、屏幕共享等需要高效捕获屏幕画面的场景。
​扩展性：可通过继承 ScreenCapture 基类，实现不同平台的捕获逻辑（如 macOS 的 avfoundation）。
*/

/*
直接使用 Windows 的 ​GDI（Graphics Device Interface）​ 实现屏幕捕获是可行的，但它和通过 ​FFmpeg 的 gdigrab 方案有本质区别。以下是两者的对比和选择 FFmpeg 的核心原因：

​1. 直接使用 GDI 的局限性
​**(1) 功能单一，仅能获取原始像素**
GDI 的 BitBlt 或 PrintWindow 函数可以直接从屏幕或窗口的设备上下文（DC）中复制像素数据，但输出的是未压缩的位图（如 BMP 格式）。
​问题：若需要实时编码（如 H.264）、格式转换（如 YUV420P）或推流（RTMP），需手动实现编解码逻辑，开发成本极高。
​**(2) 性能瓶颈**
​高分辨率/高帧率场景：直接操作 GDI 的像素数据是 CPU 密集型操作，例如 4K 屏幕的逐帧捕获会导致 CPU 负载激增。
​数据冗余：未压缩的原始位图数据量极大（例如 1920x1080 的 32 位色深帧大小约 8MB），对内存和传输带宽不友好。
​**(3) 多线程与同步问题**
若需要实现后台线程持续捕获，需自行管理线程安全、帧缓冲队列、资源锁（mutex）等复杂逻辑。
​**(4) 硬件加速缺失**
GDI 本身不支持 GPU 加速（如 DirectX 或 CUDA），难以利用现代硬件的编解码能力（如 NVENC）。

​2. 为什么选择 FFmpeg？
FFmpeg 的 gdigrab 设备封装了 GDI 的底层操作，并提供了完整的多媒体处理流水线，核心优势如下：

​**(1) 集成编解码与流处理**
​直接输出压缩数据：通过 FFmpeg 的编码器（如 libx264），可将原始像素实时压缩为 H.264/H.265，减少 90% 以上的数据量。
​格式转换：通过 swscale 库，轻松将 GDI 的 BGRA 像素转换为标准 YUV420P 等格式。
​推流支持：集成 RTMP、RTSP 等协议，可直接将屏幕画面推送至直播服务器。
​**(2) 高性能优化**
​多线程解码/编码：FFmpeg 自动利用多核 CPU 并行处理。
​硬件加速：支持 DXVA2、CUDA、QSV 等硬件编解码方案，大幅降低 CPU 负载。
​零拷贝（Zero-Copy）​：部分场景下可避免内存复制，提升效率。
​**(3) 简化开发**
​统一接口：所有操作（捕获、编码、封装、推流）通过 FFmpeg API 完成，代码更简洁。
​跨平台：同一套代码稍作修改即可支持 Linux（x11grab）或 macOS（avfoundation）。
​**(4) 社区与生态**
​成熟稳定：FFmpeg 是经过工业级验证的多媒体框架，修复了无数底层兼容性问题（如不同 Windows 版本的 GDI 行为差异）。
​功能扩展性：可轻松添加音频捕获、滤镜（缩放、水印）、多路流合成等高级功能。

​3. 直接使用 GDI 的适用场景
仅在以下简单需求中可考虑直接使用 GDI：

​单次截图：不需要连续捕获，只需偶尔获取屏幕静态图像。
​极简依赖：无法引入第三方库（如 FFmpeg）的极端场景。
​学习目的：理解 GDI 的工作原理。

4.  结论
​优先选择 FFmpeg：除非有严格的依赖限制，否则 FFmpeg 是屏幕捕获的最优解，兼顾性能、功能和开发效率。
​直接使用 GDI 仅限简单场景：适合快速实现单次截图，但无法应对实时性、压缩或流媒体需求。

*/

#include "ScreenCapture.h"   // 基类，定义屏幕捕获的通用接口
#include "WindowHelper.h"   // 可能用于获取显示器信息
extern "C" {
#include "libavcodec/avcodec.h"   // FFmpeg 编解码
#include "libavdevice/avdevice.h" // FFmpeg 设备输入（如 gdigrab）
#include "libavformat/avformat.h" // FFmpeg 格式处理
#include "libswscale/swscale.h"   // 图像缩放/格式转换
}
#include <memory>  // 智能指针
#include <thread>  // 多线程支持
#include <mutex>   // 线程同步

/*
基类继承：继承自 ScreenCapture，需实现其定义的接口（如 Init, CaptureFrame）。
​FFmpeg 依赖：使用 gdigrab 设备捕获屏幕，依赖 FFmpeg 的编解码和格式处理库。
​多线程与同步：通过 std::thread 和 std::mutex 实现后台捕获线程与前端的数据同步。
*/

class GDIScreenCapture : public ScreenCapture
{

/*
关键功能：初始化、销毁、捕获帧、获取状态，符合屏幕捕获的通用操作流程。
*/
public:
	GDIScreenCapture();          // 构造函数
	virtual ~GDIScreenCapture(); // 虚析构函数（确保正确释放资源）

	// 初始化屏幕捕获（指定显示器索引）
	virtual bool Init(int display_index = 0);

	// 释放资源
	virtual bool Destroy();

	// 捕获一帧图像，返回数据、宽高
	virtual bool CaptureFrame(std::vector<uint8_t>& image, uint32_t& width, uint32_t& height);

	// 获取当前捕获图像的宽高
	virtual uint32_t GetWidth() const;
	virtual uint32_t GetHeight() const;

	// 检查捕获是否已启动
	virtual bool CaptureStarted() const;

/*
FFmpeg 核心对象：
	format_context_：管理输入流（如屏幕捕获设备）。
	codec_context_：解码视频流（如将原始数据转换为 RGB 格式）。
​
线程与同步：
	thread_ptr_ 运行后台循环，持续捕获和解码帧。
	mutex_ 确保 image_、width_、height_ 在多线程访问时的安全性。
​
图像存储：
	image_ 使用 std::shared_ptr 管理内存，可能通过 FFmpeg 接口分配（需自定义释放器）。
*/
private:
private:
    bool StartCapture();    // 启动后台捕获线程
    void StopCapture();     // 停止线程
    bool AquireFrame();     // 从 FFmpeg 获取数据包
    bool Decode(AVFrame* av_frame, AVPacket* av_packet); // 解码数据包到帧

    // 成员变量
    DX::Monitor monitor_;   // 显示器信息（如坐标、尺寸）
    bool is_initialized_ = false; // 初始化状态
    bool is_started_ = false;     // 捕获线程是否启动
    std::unique_ptr<std::thread> thread_ptr_; // 后台线程指针

    // FFmpeg 相关
    AVFormatContext* format_context_ = nullptr; // 输入流上下文
    AVInputFormat* input_format_ = nullptr;     // 输入格式（gdigrab）
    AVCodecContext* codec_context_ = nullptr;    // 解码器上下文
    int video_index_ = -1;       // 视频流索引
    int framerate_ = 25;         // 捕获帧率（默认25 FPS）

    // 图像数据与同步
    std::mutex mutex_;                     // 保护共享数据
    std::shared_ptr<uint8_t> image_;       // 存储捕获的图像数据
    uint32_t image_size_ = 0;              // 图像数据大小
    uint32_t width_ = 0, height_ = 0;      // 当前图像的宽高
};


#endif

/*
功能流程
​
初始化 (Init)：
    调用 DX::GetMonitors 获取显示器信息。
    配置 FFmpeg 的 gdigrab 设备参数（如帧率、捕获区域）。
    打开输入流（avformat_open_input），查找视频流，初始化解码器。
    
启动捕获 (StartCapture)：
    创建后台线程，循环执行：
        读取数据包（av_read_frame）。
        解码数据包为 AVFrame（Decode 函数）。
        将解码后的图像数据转换为目标格式（如 RGB），存储到 image_。
​
获取帧数据 (CaptureFrame)：
    加锁（mutex_）保护共享数据。
    将 image_ 的数据复制到 std::vector<uint8_t>& image。
    更新输出的 width 和 height。

​资源释放 (Destroy)：
    停止线程（StopCapture）。
    释放 FFmpeg 资源（avformat_close_input, avcodec_free_context）。
    重置状态标志（is_initialized_, is_started_）。
*/