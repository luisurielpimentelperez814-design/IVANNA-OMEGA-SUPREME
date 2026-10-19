// SceneGeometryRenderer.hpp — bounded CPU renderer for IVANNA room geometry.
// Produces a deterministic 4K PPM wireframe from the same metre-based geometry
// used by RoomGeometryConfig. No GPU, external library, network, or shell access.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace ivanna::spatial::render {

struct Vec3 { float x=0.f, y=0.f, z=0.f; };
struct Scene {
    float widthM=3.5f, heightM=3.5f, depthM=7.f;
    Vec3 listener{1.75f,1.2f,3.5f};
    std::array<Vec3,7> speakers{{
        {0.35f,1.5f,0.f},{3.15f,1.5f,0.f},{0.25f,1.5f,3.5f},
        {3.25f,1.5f,3.5f},{0.25f,3.f,3.5f},{3.25f,3.f,3.5f},{3.f,0.15f,3.f}
    }};
};
struct RenderStats { std::uint32_t width=0,height=0; std::size_t bytes=0; std::size_t segments=0; };

class SceneGeometryRenderer {
public:
    static constexpr std::uint32_t k4kWidth=3840, k4kHeight=2160;
    static constexpr std::size_t kMaxPixels=3840u*2160u;
    static constexpr float kMaxRoomDimensionM=100.f;

    static bool valid(const Scene& s) noexcept {
        if (!finitePositive(s.widthM)||!finitePositive(s.heightM)||!finitePositive(s.depthM) ||
            s.widthM>kMaxRoomDimensionM||s.heightM>kMaxRoomDimensionM||s.depthM>kMaxRoomDimensionM) return false;
        if (!inside(s.listener,s)) return false;
        for (const auto& p:s.speakers) if (!inside(p,s)) return false;
        return true;
    }

    // Render a perspective-like axonometric wireframe into binary PPM (P6).
    // Dimensions are capped to 4K and allocation is bounded to 24,883,200 bytes.
    static bool renderPpm(const Scene& scene, const std::string& path,
                          std::uint32_t width=k4kWidth, std::uint32_t height=k4kHeight,
                          RenderStats* stats=nullptr) {
        if (!valid(scene)||path.empty()||width==0||height==0||width>k4kWidth||height>k4kHeight) return false;
        const std::size_t pixels=static_cast<std::size_t>(width)*height;
        if (pixels>kMaxPixels||pixels>std::numeric_limits<std::size_t>::max()/3u) return false;
        std::vector<std::uint8_t> image(pixels*3u, 12u);
        auto put=[&](int x,int y,std::array<std::uint8_t,3> c) {
            if(x<0||y<0||x>=static_cast<int>(width)||y>=static_cast<int>(height)) return;
            const auto i=(static_cast<std::size_t>(y)*width+static_cast<std::size_t>(x))*3u;
            image[i]=c[0];image[i+1]=c[1];image[i+2]=c[2];
        };
        const float scale=std::min(width/(scene.widthM+scene.depthM*0.62f+1.f),
                                   height/(scene.heightM+scene.depthM*0.45f+1.f))*0.78f;
        const float ox=width*0.5f, oy=height*0.78f;
        auto project=[&](Vec3 p) {
            return std::array<int,2>{static_cast<int>(ox+(p.x-scene.widthM*0.5f)*scale+(p.z-scene.depthM*0.5f)*scale*0.62f),
              static_cast<int>(oy-(p.y*scale)-(p.z*scale*0.45f))};
        };
        std::size_t segments=0;
        auto line=[&](Vec3 a,Vec3 b,std::array<std::uint8_t,3> color) {
            auto p=project(a),q=project(b); int x=p[0],y=p[1],x1=q[0],y1=q[1];
            const int dx=std::abs(x1-x),sx=x<x1?1:-1,dy=-std::abs(y1-y),sy=y<y1?1:-1;
            int err=dx+dy;
            for(std::size_t guard=0;guard<static_cast<std::size_t>(width)+height+4u;++guard){
                put(x,y,color);if(x==x1&&y==y1)break;const int e2=2*err;
                if(e2>=dy){err+=dy;x+=sx;}if(e2<=dx){err+=dx;y+=sy;}
            } ++segments;
        };
        const std::array<Vec3,8> c{{{0,0,0},{scene.widthM,0,0},{scene.widthM,scene.heightM,0},{0,scene.heightM,0},
          {0,0,scene.depthM},{scene.widthM,0,scene.depthM},{scene.widthM,scene.heightM,scene.depthM},{0,scene.heightM,scene.depthM}}};
        constexpr int edges[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
        for(const auto& e:edges)line(c[e[0]],c[e[1]],{60,130,170});
        auto marker=[&](Vec3 p,std::array<std::uint8_t,3> col,int radius){
            auto q=project(p);for(int yy=-radius;yy<=radius;++yy)for(int xx=-radius;xx<=radius;++xx)
                if(xx*xx+yy*yy<=radius*radius)put(q[0]+xx,q[1]+yy,col);
        };
        for(const auto& p:scene.speakers)marker(p,{60,210,150},std::max(3,static_cast<int>(width/600)));
        marker(scene.listener,{255,190,65},std::max(4,static_cast<int>(width/500)));
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        if(!out)return false;
        out<<"P6\n"<<width<<" "<<height<<"\n255\n";
        out.write(reinterpret_cast<const char*>(image.data()),static_cast<std::streamsize>(image.size()));
        if(!out)return false;
        if(stats){stats->width=width;stats->height=height;stats->bytes=image.size();stats->segments=segments;}
        return true;
    }
private:
    static bool finitePositive(float x) noexcept { return std::isfinite(x)&&x>0.f; }
    static bool inside(Vec3 p,const Scene& s) noexcept {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&
          p.x>=0.f&&p.x<=s.widthM&&p.y>=0.f&&p.y<=s.heightM&&p.z>=0.f&&p.z<=s.depthM;
    }
};
} // namespace ivanna::spatial::render
