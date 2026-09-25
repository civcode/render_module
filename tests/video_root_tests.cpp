#include "render_module/render_module.hpp"
#include "render_module/video.hpp"
#include "core/render_output.hpp"
#include "present/image_presenter.hpp"
#include <imgui_internal.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef VIDEO_TEST_DECODER
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif
using namespace render_module;
using namespace render_module::video;
#define CHECK(x) do { if (!(x)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #x); } while(false)
namespace {
void Scene(int& frames,int count,bool resize) {
    RenderModule::EnableRootWindowDocking();
    RenderModule::RegisterImGuiCallback([&,count,resize] {
        ++frames;
        if(frames==1) {
            const auto root=RenderModule::GetRootDockspaceID(); auto size=ImGui::GetIO().DisplaySize;
            ImGui::DockBuilderRemoveNode(root); ImGui::DockBuilderAddNode(root,ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(root,size);
            ImGuiID left,right,controls,plot,canvas,view;
            ImGui::DockBuilderSplitNode(root,ImGuiDir_Left,.5f,&left,&right);
            ImGui::DockBuilderSplitNode(left,ImGuiDir_Up,.46f,&controls,&plot);
            ImGui::DockBuilderSplitNode(right,ImGuiDir_Up,.46f,&canvas,&view);
            ImGui::DockBuilderDockWindow("Controls",controls); ImGui::DockBuilderDockWindow("Plot",plot);
            ImGui::DockBuilderDockWindow("Canvas",canvas); ImGui::DockBuilderDockWindow("View3D",view);
            ImGui::DockBuilderFinish(root);
        }
        ImGui::Begin("Controls"); ImGui::TextUnformatted("Full-root realtime video UI / BT.709");
        ImGui::Button("Static button",{150,26}); bool checked=true; ImGui::Checkbox("Composition enabled",&checked);
        float value=.375f; ImGui::SliderFloat("Value",&value,0,1); ImGui::End();
        ImGui::Begin("Plot");
        if(ImPlot::BeginPlot("Fixed data",{-1,-1},ImPlotFlags_NoInputs|ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("x","y"); ImPlot::SetupAxesLimits(0,4,0,1,ImGuiCond_Always);
            const double y[]={.2,.8,.4,.7,.3}; ImPlot::PlotLine("series",y,5); ImPlot::EndPlot();
        } ImGui::End();
        auto* draw=ImGui::GetForegroundDrawList();
        const float height=ImGui::GetIO().DisplaySize.y;
        draw->AddRectFilled({4,4},{40,24},IM_COL32(255,0,0,255));
        draw->AddRectFilled({4,height-24},{40,height-4},IM_COL32(0,0,255,255));
        if(resize && frames==15) CHECK(detail::RequestVirtualDisplaySize(1299,723));
        if(resize && frames==30) CHECK(detail::RequestVirtualDisplaySize(1919,1079));
        if(frames==count) RenderModule::RequestClose();
    });
    RenderModule::RegisterCanvas("Canvas",[](Canvas& canvas) {
        auto* vg=canvas.Graphics(); nvgBeginPath(vg); nvgRect(vg,12,12,185,98);
        nvgFillColor(vg,nvgRGB(230,35,45)); nvgFill(vg);
        nvgBeginPath(vg); nvgCircle(vg,canvas.Size().x*.6f,canvas.Size().y*.62f,54);
        nvgFillColor(vg,nvgRGB(255,200,30)); nvgFill(vg);
    });
    View3DOptions options; options.showStatus=false;
    RenderModule::Register3DView("View3D",[](View3D& view) {
        view.Camera().LookAt({3,-4,3},{0,0,0}); view.Grid(2,.5f);
        view.Box("fixed",{},{1.2f,1.2f,1.2f},{.15f,.65f,.95f,1}); view.Axes("axes",{},1.2f);
    },options);
}
void Print(std::ostream& out,const char* name,const Timing& t) {
    out<<'"'<<name<<"\":{\"samples\":"<<t.samples<<",\"mean_ms\":"<<t.MeanMs()<<",\"worst_ms\":"<<t.worstUs/1000.<<'}';
}
void Run(int width,int height,bool resize,const std::string& path) {
    Config config; config.backend=Backend::Headless; config.headlessContext=HeadlessContext::NativeEgl;
    config.width=width; config.height=height; config.fps=30;
    CHECK(RenderModule::Init(config)); ImGui::GetIO().IniFilename=nullptr;
    auto pipeline=std::make_shared<VideoPipeline>(); CHECK(RenderModule::SetVideoOutput(pipeline));
    int frames=0; Scene(frames,60,resize);
    std::atomic<bool> done{false}; std::exception_ptr consumerError;
    std::uint64_t received=0,lastId=0; std::int64_t lastPts=-1;
    int decodedWidth=0,decodedHeight=0;
    std::vector<unsigned char> decodedRgb;
    std::ofstream bitstream(path+".h264",std::ios::binary);
    std::thread consumer([&] {
        try {
#ifdef VIDEO_TEST_DECODER
            struct Decoder {
                AVCodecContext* context=nullptr; AVFrame* frame=av_frame_alloc(); AVPacket* packet=av_packet_alloc();
                Decoder() { auto* codec=avcodec_find_decoder(AV_CODEC_ID_H264); CHECK(codec);
                    context=avcodec_alloc_context3(codec); CHECK(context && frame && packet);
                    context->thread_count=1; CHECK(avcodec_open2(context,codec,nullptr)>=0); }
                ~Decoder() { avcodec_free_context(&context); av_frame_free(&frame); av_packet_free(&packet); }
            } decoder;
#endif
            for(;;) {
                EncodedFrame e;
                if(!pipeline->TryReceive(e)) { if(done) break; std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
                CHECK(e.frameId>lastId && e.ptsUs>lastPts); lastId=e.frameId; lastPts=e.ptsUs; ++received;
                bitstream.write(reinterpret_cast<const char*>(e.storage->data()),e.size); CHECK(bitstream.good());
#ifdef VIDEO_TEST_DECODER
                av_packet_unref(decoder.packet); CHECK(av_new_packet(decoder.packet,int(e.size))>=0);
                std::memcpy(decoder.packet->data,e.storage->data(),e.size);
                CHECK(avcodec_send_packet(decoder.context,decoder.packet)>=0);
                CHECK(avcodec_receive_frame(decoder.context,decoder.frame)>=0);
                auto* f=decoder.frame; decodedWidth=f->width; decodedHeight=f->height;
                CHECK(decodedWidth==e.width && decodedHeight==e.height);
                CHECK(f->colorspace==AVCOL_SPC_BT709 && f->color_range==AVCOL_RANGE_MPEG);
                // Asymmetric root markers check orientation/R-B throughout the stream.
                CHECK(std::abs(int(f->data[0][12*f->linesize[0]+12])-63)<12);
                CHECK(std::abs(int(f->data[0][(e.height-12)*f->linesize[0]+12])-32)<12);
                if(e.frameId==60) {
                    decodedRgb.resize(std::size_t(e.width)*e.height*3);
                    for(int y=0;y<e.height;++y) for(int x=0;x<e.width;++x) {
                        const double yy=(f->data[0][y*f->linesize[0]+x]-16)*255./219.;
                        const double u=(f->data[1][(y/2)*f->linesize[1]+x/2]-128)*255./224.;
                        const double v=(f->data[2][(y/2)*f->linesize[2]+x/2]-128)*255./224.;
                        const double rgb[]={yy+1.5748*v,yy-.187324*u-.468124*v,yy+1.8556*u};
                        for(int ch=0;ch<3;++ch) decodedRgb[(std::size_t(y)*e.width+x)*3+ch]=std::uint8_t(std::clamp(rgb[ch],0.,255.));
                    }
                }
#endif
            }
        } catch(...) { consumerError=std::current_exception(); }
    });
    try { RenderModule::Run(); CHECK(pipeline->Flush(std::chrono::seconds(5))); }
    catch(...) { done=true; consumer.join(); RenderModule::Shutdown(); throw; }
    done=true; consumer.join(); if(consumerError) std::rethrow_exception(consumerError);
    CHECK(frames==60 && received>30 && lastId==60);
    CHECK(RenderModule::SaveScreenshot(path+".png"));
    const auto root=detail::CompletedFrame();
    const auto metrics=pipeline->Metrics();
    double rgbMean=0; int rgbWorst=0;
    CHECK(metrics.errors==0 && metrics.framesSubmitted==60 && metrics.framesEncoded==received);
#ifdef VIDEO_TEST_DECODER
    CHECK(decodedWidth==(root.width&~1) && decodedHeight==(root.height&~1));
    detail::ImageRgba reference; CHECK(detail::ImagePresenter::Read(root,reference));
    CHECK(decodedRgb.size()==std::size_t(decodedWidth)*decodedHeight*3);
    for(int y=0;y<decodedHeight;++y) for(int x=0;x<decodedWidth;++x) for(int ch=0;ch<3;++ch) {
        const int error=std::abs(int(decodedRgb[(std::size_t(y)*decodedWidth+x)*3+ch])-
            reference.pixels[(std::size_t(y)*reference.width+x)*4+ch]);
        rgbMean+=error; rgbWorst=std::max(rgbWorst,error);
    }
    rgbMean/=decodedRgb.size(); CHECK(rgbMean<12);
#endif
    std::ofstream out(path+".json"); out<<"{\"width\":"<<width<<",\"height\":"<<height<<",\"target_fps\":30,";
    Print(out,"readback",metrics.readback); out<<','; Print(out,"rgba_to_i420",metrics.rgbaToI420); out<<',';
    Print(out,"encode",metrics.encode);
    out<<",\"frames_submitted\":"<<metrics.framesSubmitted<<",\"frames_encoded\":"<<metrics.framesEncoded
       <<",\"frames_dropped\":"<<metrics.framesDropped<<",\"bytes_encoded\":"<<metrics.bytesEncoded
       <<",\"keyframes\":"<<metrics.keyframes<<",\"mean_bytes_per_frame\":"<<double(metrics.bytesEncoded)/received
       <<",\"observed_bps_at_30fps\":"<<double(metrics.bytesEncoded)*8/2
       <<",\"ui_rgb_mean_error\":"<<rgbMean<<",\"ui_rgb_worst_error\":"<<rgbWorst<<"}\n";
    std::cout<<width<<'x'<<height<<" readback="<<metrics.readback.MeanMs()<<"ms convert="<<metrics.rgbaToI420.MeanMs()
             <<"ms encode="<<metrics.encode.MeanMs()<<"ms frames="<<received<<" bytes="<<metrics.bytesEncoded<<'\n';
    CHECK(glGetError()==GL_NO_ERROR); CHECK(RenderModule::SetVideoOutput(nullptr)); RenderModule::Shutdown();
}
}
int main(int argc,char** argv) {
    try { CHECK(argc==3); std::filesystem::create_directories(argv[2]);
        const std::string mode=argv[1],path=argv[2];
        if(mode=="resize") Run(1001,701,true,path+"/resize");
        else { Run(1000,700,false,path+"/1000x700"); Run(1280,720,false,path+"/1280x720"); Run(1920,1080,false,path+"/1920x1080"); }
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; RenderModule::Shutdown(); return 1; }
}
