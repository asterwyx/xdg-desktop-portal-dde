// SPDX-FileCopyrightText: 2024 UnionTech Software Technology Co., Ltd.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once
#include <private/qwaylandclientextension_p.h>
#include <qwayland-wlr-screencopy-unstable-v1.h>

#include <QObject>

#include <type_traits>

class ScreenCopyManager;
inline void destruct_screen_copy_manager(ScreenCopyManager *screenCopyManager);

class ScreenCopyManager
    : public QWaylandClientExtensionTemplate<ScreenCopyManager, destruct_screen_copy_manager>,
      public QtWayland::zwlr_screencopy_manager_v1
{
    Q_OBJECT
public:
    explicit ScreenCopyManager()
        : QWaylandClientExtensionTemplate<ScreenCopyManager, destruct_screen_copy_manager>(1)
        , QtWayland::zwlr_screencopy_manager_v1()
    {
    }

    template<typename T, typename... Args>
    inline T *captureOutput(int32_t overlay_cursor, ::wl_output *output, Args... args)
    {
        static_assert(std::is_base_of_v<QtWayland::zwlr_screencopy_frame_v1, T>);
        auto o = capture_output(overlay_cursor, output);
        auto wrapper = new T(o, args...);
        m_frameList.append(wrapper);
        return wrapper;
    }

    template<typename T, typename... Args>
    inline T *captureOutputRegion(int32_t overlay_cursor,
                                  struct ::wl_output *output,
                                  int32_t x,
                                  int32_t y,
                                  int32_t width,
                                  int32_t height,
                                  Args... args)
    {
        static_assert(std::is_base_of_v<QtWayland::zwlr_screencopy_frame_v1, T>);
        auto o = capture_output_region(overlay_cursor, output, x, y, width, height);
        auto wrapper = new T(o, args...);
        m_frameList.append(wrapper);
        return wrapper;
    }

private:
    QList<QtWayland::zwlr_screencopy_frame_v1 *> m_frameList;
    friend void destruct_screen_copy_manager(ScreenCopyManager *screenCopyManager);
};

inline void destruct_screen_copy_manager(ScreenCopyManager *screenCopyManager)
{
    qDeleteAll(screenCopyManager->m_frameList);
    screenCopyManager->m_frameList.clear();
}
