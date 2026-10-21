#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include "core/Util.h"
#include "import/Importer.h"
#include "app/App.h"
#include "ufbx.h"
#include "tracking/FaceTracker.h"
#include "tracking/HandTracker.h"
#include "stb_image.h"
#include "stb_image_write.h"

static void attachConsole() {
    // ha a kimenet át van irányítva (pipe/fájl), azt használjuk
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD type = h && h != INVALID_HANDLE_VALUE ? GetFileType(h) : FILE_TYPE_UNKNOWN;
    if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK) {
        int fd = _open_osfhandle((intptr_t)h, _O_TEXT);
        if (fd >= 0) {
            _dup2(fd, 1);
            freopen_s((FILE**)stdout, "NUL", "w", stdout);
            _dup2(fd, _fileno(stdout));
            setvbuf(stdout, nullptr, _IONBF, 0);
            return;
        }
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        FILE* f;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        SetConsoleOutputCP(CP_UTF8);
    }
}

void installCrashHandler();

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    installCrashHandler();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    // Parancssoros import: AvatarCam.exe --import <fájl>
    if (argc >= 3 && std::wstring(argv[1]) == L"--import") {
        attachConsole();
        std::string err;
        auto ids = importAvatarFile(argv[2], [](const std::string& m, float) { printf("%s\n", m.c_str()); fflush(stdout); }, &err);
        if (ids.empty()) {
            printf("Error: %s\n", err.c_str());
            return 1;
        }
        for (auto& id : ids) printf("Imported: %s\n", id.c_str());
        return 0;
    }
    // FBX diagnosztika: AvatarCam.exe --dumpfbx <fájl>
    if (argc >= 3 && std::wstring(argv[1]) == L"--dumpfbx") {
        std::string path = narrow(argv[2]);
        FILE* out = nullptr;
        if (argc >= 4) _wfopen_s(&out, argv[3], L"w");
        if (!out) return 2;
        for (int mode = 0; mode < 2; mode++) {
            ufbx_load_opts o = {};
            if (mode == 1) {
                o.target_axes = ufbx_axes_right_handed_y_up;
                o.target_unit_meters = 1.0f;
                o.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
            }
            ufbx_error e;
            ufbx_scene* sc = ufbx_load_file(path.c_str(), &o, &e);
            if (!sc) {
                fprintf(out, "hiba\n");
                return 1;
            }
            fprintf(out, "== mode %d: unit_meters=%.4f original=%.4f\n", mode, sc->settings.unit_meters, sc->settings.original_unit_meters);
            for (size_t i = 0; i < sc->nodes.count && i < 40; i++) {
                ufbx_node* n = sc->nodes.data[i];
                ufbx_transform t = n->local_transform;
                fprintf(out, "  %-30s T(%.4f %.4f %.4f) S(%.4f %.4f %.4f) mesh=%s verts=%zu\n", n->name.data, t.translation.x, t.translation.y, t.translation.z, t.scale.x, t.scale.y,
                       t.scale.z, n->mesh ? "igen" : "-", n->mesh ? n->mesh->num_vertices : (size_t)0);
                if (n->mesh && n->mesh->num_vertices) {
                    ufbx_vec3 mn = n->mesh->vertex_position.values.data[0], mx = mn;
                    for (size_t v = 0; v < n->mesh->vertex_position.values.count; v++) {
                        ufbx_vec3 p = n->mesh->vertex_position.values.data[v];
                        mn.x = std::min(mn.x, p.x), mn.y = std::min(mn.y, p.y), mn.z = std::min(mn.z, p.z);
                        mx.x = std::max(mx.x, p.x), mx.y = std::max(mx.y, p.y), mx.z = std::max(mx.z, p.z);
                    }
                    fprintf(out, "      bbox %.4f %.4f %.4f  geom_to_node scale %.4f\n", mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, n->geometry_to_node.cols[0].x);
                }
            }
            ufbx_free_scene(sc);
        }
        fclose(out);
        return 0;
    }
    // Kézkövetés teszt egy képen: AvatarCam.exe --hand-test <kép> <kimenet.png> <napló.txt>
    if (argc >= 5 && std::wstring(argv[1]) == L"--hand-test") {
        FILE* out = nullptr;
        _wfopen_s(&out, argv[4], L"w");
        if (!out) return 2;
        int w, h, c;
        stbi_uc* px = stbi_load(narrow(argv[2]).c_str(), &w, &h, &c, 4);
        if (!px) {
            fprintf(out, "kep hiba\n");
            return 1;
        }
        CameraFrame f;
        f.w = w, f.h = h;
        f.bgra.resize((size_t)w * h * 4);
        for (int i = 0; i < w * h; i++) f.bgra[i * 4] = px[i * 4 + 2], f.bgra[i * 4 + 1] = px[i * 4 + 1], f.bgra[i * 4 + 2] = px[i * 4], f.bgra[i * 4 + 3] = 255;
        HandTracker ht;
        HandResult r;
        std::string err;
        double t0 = nowSeconds();
        for (int i = 0; i < 4; i++) ht.process(f, r, err);
        fprintf(out, "err=%s time=%.1fms\n", err.c_str(), (nowSeconds() - t0) * 1000 / 4);
        for (int u = 0; u < 2; u++) {
            const HandObs& o = r.hands[u];
            fprintf(out, "hand[%d] valid=%d userRight=%d score=%.2f handedness=%.2f wrist=(%.0f %.0f) cam=(%.2f %.2f %.2f)\n", u, (int)o.valid, (int)o.userRight, o.score,
                    o.handedness, o.lms[0].x, o.lms[0].y, o.wristCam.x, o.wristCam.y, o.wristCam.z);
            if (!o.valid) continue;
            fprintf(out, "   world0-9 len=%.3f  world0-5=%.3f\n", length(o.world[9] - o.world[0]), length(o.world[5] - o.world[0]));
            auto dot = [&](float x, float y, uint8_t R, uint8_t G, uint8_t B) {
                for (int dy = -2; dy <= 2; dy++)
                    for (int dx = -2; dx <= 2; dx++) {
                        int X = (int)x + dx, Y = (int)y + dy;
                        if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                        stbi_uc* p = &px[((size_t)Y * w + X) * 4];
                        p[0] = R, p[1] = G, p[2] = B;
                    }
            };
            for (int i = 0; i < 21; i++) dot(o.lms[i].x, o.lms[i].y, u ? 255 : 40, u ? 140 : 255, 40);
        }
        stbi_write_png(narrow(argv[3]).c_str(), w, h, 4, px, w * 4);
        fclose(out);
        return 0;
    }
    // Arckövetés teszt egy képen: AvatarCam.exe --track-test <kép> <kimenet.png> <napló.txt>
    if (argc >= 5 && std::wstring(argv[1]) == L"--track-test") {
        FILE* out = nullptr;
        _wfopen_s(&out, argv[4], L"w");
        if (!out) return 2;
        int w, h, c;
        stbi_uc* px = stbi_load(narrow(argv[2]).c_str(), &w, &h, &c, 4);
        if (!px) {
            fprintf(out, "image error\n");
            return 1;
        }
        CameraFrame f;
        f.w = w, f.h = h;
        f.bgra.resize((size_t)w * h * 4);
        for (int i = 0; i < w * h; i++) f.bgra[i * 4] = px[i * 4 + 2], f.bgra[i * 4 + 1] = px[i * 4 + 1], f.bgra[i * 4 + 2] = px[i * 4], f.bgra[i * 4 + 3] = 255;
        FaceTracker ft;
        FaceResult r;
        std::string err;
        double t0 = nowSeconds();
        bool ok = false;
        for (int i = 0; i < 3; i++) ok = ft.processImage(f, r, err);  // többször, hogy a PnP konvergáljon
        fprintf(out, "ok=%d err=%s conf=%.3f time=%.1fms\n", (int)ok, err.c_str(), r.conf, (nowSeconds() - t0) * 1000 / 3);
        if (ok) {
            Quat q = r.head;
            // yaw (Y), pitch (X), roll (Z) közelítő bontás
            Vec3 fwd = q.rotate({0, 0, 1}), up = q.rotate({0, 1, 0});
            fprintf(out, "yaw=%.1f pitch=%.1f roll=%.1f  trans=(%.2f %.2f %.2f)\n", std::atan2(fwd.x, fwd.z) * kRad2Deg, std::asin(clampf(-fwd.y, -1, 1)) * kRad2Deg,
                    std::atan2(-up.x, up.y) * kRad2Deg, r.trans.x, r.trans.y, r.trans.z);
            fprintf(out, "eyeOpenL=%.2f eyeOpenR=%.2f mouth=%.2f gaze=(%.2f %.2f)\n", r.eyeOpenL, r.eyeOpenR, r.mouthOpen, r.gazeX, r.gazeY);
            auto dot = [&](float x, float y, uint8_t R, uint8_t G, uint8_t B) {
                int rad = std::max(1, w / 300);
                for (int dy = -rad; dy <= rad; dy++)
                    for (int dx = -rad; dx <= rad; dx++) {
                        int X = (int)x + dx, Y = (int)y + dy;
                        if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                        stbi_uc* p = &px[((size_t)Y * w + X) * 4];
                        p[0] = R, p[1] = G, p[2] = B;
                    }
            };
            for (auto& p : r.lms) dot(p.x, p.y, 40, 255, 80);
            if (r.gazeValid)
                for (auto& p : r.pupils) dot(p.x, p.y, 255, 40, 40);
        }
        stbi_write_png(narrow(argv[3]).c_str(), w, h, 4, px, w * 4);
        fclose(out);
        return 0;
    }
    App app;
    std::vector<std::wstring> args(argv + 1, argv + argc);
    LocalFree(argv);
    return app.run(hInst, args);
}
