#pragma once

#include "mcbe/Render/CameraComponent.hpp"
#include "mcbe/Render/LevelRenderer.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <atomic>
#include <cmath>
#include <mutex>

namespace mcbe {

class WorldPanelDemo {
public:
    struct Transform {
        Vec3 center{};
        Vec3 right{1.f, 0.f, 0.f};
        Vec3 up{0.f, 1.f, 0.f};
        float width{2.8f};
        bool valid{};
    };

    void updateCamera(const world::CameraComponent* camera) {
        if (!camera)
            return;

        Snapshot next{};
        next.origin = camera->origin;
        next.forward = rotate(camera->quat, {0.f, 0.f, -1.f});
        if (!normalize(next.forward))
            return;

        // Keep proof panels upright in Minecraft world-space. Rebuilding the
        // lateral basis from forward + world-up avoids camera roll/layout quirks
        // ever flipping the ImGui window vertically.
        constexpr Vec3 worldUp{0.f, 1.f, 0.f};
        next.right = cross(next.forward, worldUp);
        if (!normalize(next.right)) {
            next.right = rotate(camera->quat, {1.f, 0.f, 0.f});
            if (!normalize(next.right))
                return;
        }

        next.up = cross(next.right, next.forward);
        if (!normalize(next.up))
            return;

        next.valid = finite(next.origin) && finite(next.forward) &&
                     finite(next.right) && finite(next.up);
        if (!next.valid)
            return;

        std::scoped_lock lock(mutex_);
        camera_ = next;
    }

    void toggleRequested() {
        std::scoped_lock lock(mutex_);
        if (panel_.valid) {
            panel_ = {};
            placeRequested_ = false;
        } else {
            placeRequested_ = true;
        }
    }

    bool active() const {
        std::scoped_lock lock(mutex_);
        return panel_.valid || placeRequested_;
    }

    Transform consumeTransform() {
        std::scoped_lock lock(mutex_);

        if (placeRequested_ && camera_.valid) {
            panel_.center = add(camera_.origin, mul(camera_.forward, 3.0f));
            panel_.right = camera_.right;
            panel_.up = camera_.up;
            panel_.width = 2.8f;
            panel_.valid = true;
            placeRequested_ = false;
        }

        return panel_;
    }

    static ImGuiWindow* sourceWindow() {
        if (!ImGui::GetCurrentContext())
            return nullptr;

        ImGuiWindow* window =
            ImGui::FindWindowByName("MCBE Tessellator ImGui Backend");
        if (!window || !window->DrawList || window->DrawList->VtxBuffer.empty())
            return nullptr;
        return window;
    }

private:
    struct Snapshot {
        Vec3 origin{};
        Vec3 forward{};
        Vec3 right{};
        Vec3 up{};
        bool valid{};
    };

    static bool finite(const Vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    static Vec3 add(const Vec3& a, const Vec3& b) {
        return {a.x + b.x, a.y + b.y, a.z + b.z};
    }

    static Vec3 mul(const Vec3& v, float scale) {
        return {v.x * scale, v.y * scale, v.z * scale};
    }

    static float lengthSq(const Vec3& v) {
        return v.x * v.x + v.y * v.y + v.z * v.z;
    }

    static bool normalize(Vec3& v) {
        const float ls = lengthSq(v);
        if (!std::isfinite(ls) || ls < 0.000001f)
            return false;
        const float inv = 1.f / std::sqrt(ls);
        v.x *= inv;
        v.y *= inv;
        v.z *= inv;
        return true;
    }

    static Vec3 cross(const Vec3& a, const Vec3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
    }

    static Vec3 rotate(const world::Quaternion& q, const Vec3& v) {
        // GLM's default quat layout is w,x,y,z. Phase 26.52 stores
        // CameraComponent::quat at 0x30 and uses q * vec3(0,0,-1).
        const Vec3 qv{q.x, q.y, q.z};
        const Vec3 t = mul(cross(qv, v), 2.f);
        return add(v, add(mul(t, q.w), cross(qv, t)));
    }

    mutable std::mutex mutex_;
    Snapshot camera_{};
    Transform panel_{};
    bool placeRequested_{};
};

} // namespace mcbe
