/****************************************************************
                       Copyright (c)
                    Microsoft Corporation
                    All Rights Reserved
               Licensed under the MIT License.
****************************************************************/

// Associated header
//
#include "k4adevicedockcontrol.h"

// System headers
//
#include <memory>
#include <sstream>
#include <utility>

// Library headers
//

// Project headers
//
#include "k4aaudiomanager.h"
#include "k4aimguiextensions.h"
#include "k4atypeoperators.h"
#include "k4aviewererrormanager.h"
#include "k4awindowmanager.h"

using namespace k4aviewer;

namespace
{
static constexpr int MaxTimeoutCount = 120;

bool HasTimedOut(k4a_wait_result_t result, int &timeoutCounter)
{
    if (result == K4A_WAIT_RESULT_SUCCEEDED)
    {
        timeoutCounter = 0;
        return false;
    }

    if (result == K4A_WAIT_RESULT_TIMEOUT)
    {
        if (timeoutCounter < MaxTimeoutCount)
        {
            ++timeoutCounter;
            return false;
        }

        return true;
    }

    return true;
}

template<typename T>
void PollSensor(K4ADevice &device,
                const char *sensorFriendlyName,
                const std::function<k4a_wait_result_t(K4ADevice &, T &)> &pollFn,
                const std::function<void(K4ADevice &)> &stopFn,
                int &timeoutCounter,
                K4ADataSource<T> &dataSource)
{
    T data;
    const k4a_wait_result_t pollStatus = pollFn(device, data);
    const bool hasTimedOut = HasTimedOut(pollStatus, timeoutCounter);

    if (pollStatus == K4A_WAIT_RESULT_SUCCEEDED)
    {
        dataSource.NotifyObservers(data);
    }
    else if (pollStatus == K4A_WAIT_RESULT_TIMEOUT && !hasTimedOut)
    {
        // do nothing
    }
    else
    {
        std::stringstream errorBuilder;
        errorBuilder << sensorFriendlyName << " on device " << device.GetSerialNumber();
        if (pollStatus == K4A_WAIT_RESULT_TIMEOUT)
        {
            errorBuilder << " timed out!";
        }
        else
        {
            errorBuilder << " failed!";
        }

        K4AViewerErrorManager::Instance().SetErrorStatus(errorBuilder.str());
        stopFn(device);
        dataSource.NotifyTermination();
    }
}
} // namespace

void K4ADeviceDockControl::CheckFirmwareVersion(const k4a_version_t actualVersion,
                                                const k4a_version_t minVersion,
                                                const char *type) const
{
    if (actualVersion < minVersion)
    {
        std::stringstream sb;
        sb << "Warning: device " << m_device->GetSerialNumber() << " has outdated " << type
           << " firmware and may not work properly!" << std::endl
           << "  Actual:   " << actualVersion.major << "." << actualVersion.minor << "." << actualVersion.iteration
           << std::endl
           << "  Minimum: " << minVersion.major << "." << minVersion.minor << "." << minVersion.iteration;
        K4AViewerErrorManager::Instance().SetErrorStatus(sb.str());
    }
}

void K4ADeviceDockControl::ShowColorControl(k4a_color_control_command_t command,
                                            ColorSetting &cacheEntry,
                                            const std::function<ColorControlAction(ColorSetting &)> &showControl)
{
    const ColorControlAction action = showControl(cacheEntry);
    if (action == ColorControlAction::None)
    {
        return;
    }

    if (action == ColorControlAction::SetManual)
    {
        cacheEntry.Mode = K4A_COLOR_CONTROL_MODE_MANUAL;
    }
    else if (action == ColorControlAction::SetAutomatic)
    {
        cacheEntry.Mode = K4A_COLOR_CONTROL_MODE_AUTO;
    }

    ApplyColorSetting(command, cacheEntry);
}

void K4ADeviceDockControl::ShowColorControlAutoButton(k4a_color_control_mode_t currentMode,
                                                      ColorControlAction &actionToUpdate,
                                                      const char *id)
{
    ImGui::PushID(id);
    if (currentMode == K4A_COLOR_CONTROL_MODE_MANUAL)
    {
        if (ImGui::Button("A"))
        {
            actionToUpdate = ColorControlAction::SetAutomatic;
        }
    }
    else
    {
        if (ImGui::Button("M"))
        {
            actionToUpdate = ColorControlAction::SetManual;
        }
    }
    ImGui::PopID();
}

void K4ADeviceDockControl::ApplyColorSetting(k4a_color_control_command_t command, ColorSetting &cacheEntry)
{
    const k4a_result_t result = m_device->SetColorControl(command, cacheEntry.Mode, cacheEntry.Value);
    if (K4A_RESULT_SUCCEEDED != result)
    {
        std::stringstream errorMessage;
        errorMessage << "Failed to adjust color parameter: " << command << "!";
        K4AViewerErrorManager::Instance().SetErrorStatus(errorMessage.str());
    }

    // The camera can decide to set a different value than the one we give it, so rather than just saving off
    // the mode we set, we read it back from the camera and cache that instead.
    //
    ReadColorSetting(command, cacheEntry);
}

void K4ADeviceDockControl::ApplyDefaultColorSettings()
{
    // The color settings get persisted in the camera's firmware, so there isn't a way
    // to know if the setting's value at the time we started K4AViewer is the default.
    // However, the default settings are the same for all devices, so we just hardcode
    // them here.
    //
    m_colorSettingsCache.ExposureTimeUs = { K4A_COLOR_CONTROL_MODE_AUTO, 15625 };
    ApplyColorSetting(K4A_COLOR_CONTROL_EXPOSURE_TIME_ABSOLUTE, m_colorSettingsCache.ExposureTimeUs);

    m_colorSettingsCache.WhiteBalance = { K4A_COLOR_CONTROL_MODE_AUTO, 4500 };
    ApplyColorSetting(K4A_COLOR_CONTROL_WHITEBALANCE, m_colorSettingsCache.WhiteBalance);

    m_colorSettingsCache.AutoExposurePriority = { K4A_COLOR_CONTROL_MODE_MANUAL, 1 };
    ApplyColorSetting(K4A_COLOR_CONTROL_AUTO_EXPOSURE_PRIORITY, m_colorSettingsCache.AutoExposurePriority);

    m_colorSettingsCache.Brightness = { K4A_COLOR_CONTROL_MODE_MANUAL, 128 };
    ApplyColorSetting(K4A_COLOR_CONTROL_BRIGHTNESS, m_colorSettingsCache.Brightness);

    m_colorSettingsCache.Contrast = { K4A_COLOR_CONTROL_MODE_MANUAL, 5 };
    ApplyColorSetting(K4A_COLOR_CONTROL_CONTRAST, m_colorSettingsCache.Contrast);

    m_colorSettingsCache.Saturation = { K4A_COLOR_CONTROL_MODE_MANUAL, 32 };
    ApplyColorSetting(K4A_COLOR_CONTROL_SATURATION, m_colorSettingsCache.Saturation);

    m_colorSettingsCache.Sharpness = { K4A_COLOR_CONTROL_MODE_MANUAL, 2 };
    ApplyColorSetting(K4A_COLOR_CONTROL_SHARPNESS, m_colorSettingsCache.Sharpness);

    m_colorSettingsCache.BacklightCompensation = { K4A_COLOR_CONTROL_MODE_MANUAL, 0 };
    ApplyColorSetting(K4A_COLOR_CONTROL_BACKLIGHT_COMPENSATION, m_colorSettingsCache.BacklightCompensation);

    m_colorSettingsCache.Gain = { K4A_COLOR_CONTROL_MODE_MANUAL, 0 };
    ApplyColorSetting(K4A_COLOR_CONTROL_GAIN, m_colorSettingsCache.Gain);

    m_colorSettingsCache.PowerlineFrequency = { K4A_COLOR_CONTROL_MODE_MANUAL, 2 };
    ApplyColorSetting(K4A_COLOR_CONTROL_POWERLINE_FREQUENCY, m_colorSettingsCache.PowerlineFrequency);
}

void K4ADeviceDockControl::ReadColorSetting(k4a_color_control_command_t command, ColorSetting &cacheEntry)
{
    const k4a_result_t result = m_device->GetColorControl(command, cacheEntry.Mode, cacheEntry.Value);
    if (K4A_RESULT_SUCCEEDED != result)
    {
        std::stringstream errorBuilder;
        errorBuilder << "Failed to read color parameter: " << command << "!";
        K4AViewerErrorManager::Instance().SetErrorStatus(errorBuilder.str());
    }
}

void K4ADeviceDockControl::LoadColorSettingsCache()
{
    // If more color controls are added, they need to be initialized here
    //
    static_assert(sizeof(m_colorSettingsCache) == sizeof(ColorSetting) * 10,
                  "Missing color setting in LoadColorSettingsCache()");

    ReadColorSetting(K4A_COLOR_CONTROL_EXPOSURE_TIME_ABSOLUTE, m_colorSettingsCache.ExposureTimeUs);
    ReadColorSetting(K4A_COLOR_CONTROL_WHITEBALANCE, m_colorSettingsCache.WhiteBalance);
    ReadColorSetting(K4A_COLOR_CONTROL_AUTO_EXPOSURE_PRIORITY, m_colorSettingsCache.AutoExposurePriority);
    ReadColorSetting(K4A_COLOR_CONTROL_BRIGHTNESS, m_colorSettingsCache.Brightness);
    ReadColorSetting(K4A_COLOR_CONTROL_CONTRAST, m_colorSettingsCache.Contrast);
    ReadColorSetting(K4A_COLOR_CONTROL_SATURATION, m_colorSettingsCache.Saturation);
    ReadColorSetting(K4A_COLOR_CONTROL_SHARPNESS, m_colorSettingsCache.Sharpness);
    ReadColorSetting(K4A_COLOR_CONTROL_BACKLIGHT_COMPENSATION, m_colorSettingsCache.BacklightCompensation);
    ReadColorSetting(K4A_COLOR_CONTROL_GAIN, m_colorSettingsCache.Gain);
    ReadColorSetting(K4A_COLOR_CONTROL_POWERLINE_FREQUENCY, m_colorSettingsCache.PowerlineFrequency);
}

void K4ADeviceDockControl::RefreshSyncCableStatus()
{
    const k4a_result_t result = m_device->GetSyncCablesConnected(m_syncInConnected, m_syncOutConnected);
    if (result != K4A_RESULT_SUCCEEDED)
    {
        K4AViewerErrorManager::Instance().SetErrorStatus("Failed to read sync cable state!");
    }
}

bool K4ADeviceDockControl::DeviceIsStarted() const
{
    return m_device->CamerasAreStarted() || m_device->ImuIsStarted() || (m_microphone && m_microphone->IsStarted());
}

K4ADeviceDockControl::K4ADeviceDockControl(std::shared_ptr<K4ADevice> device) : m_device(std::move(device))
{
    ApplyDefaultConfiguration();

    m_windowTitle = m_device->GetSerialNumber() + ": Configuration";

    m_microphone = K4AAudioManager::Instance().GetMicrophoneForDevice(m_device->GetSerialNumber());

    // Show warnings if firmware is too old
    //
    // TODO for now, this is just so we know if we're on a version with known compat issues,
    //      but in future, we may want to add a mechanism to auto-check for a new firmware
    //      version from the Internet.  If that happens, delete this.
    //
    CheckFirmwareVersion(m_device->GetVersionInfo().rgb, { 1, 2, 29 }, "RGB");
    CheckFirmwareVersion(m_device->GetVersionInfo().depth, { 1, 2, 21 }, "Depth");
    CheckFirmwareVersion(m_device->GetVersionInfo().audio, { 0, 3, 1 }, "Microphone");

    LoadColorSettingsCache();
    RefreshSyncCableStatus();
}

K4ADeviceDockControl::~K4ADeviceDockControl()
{
    K4AWindowManager::Instance().ClearWindows();
    CloseDevice();
}

void K4ADeviceDockControl::Show()
{
    std::stringstream labelBuilder;
    labelBuilder << "Device S/N: " << m_device->GetSerialNumber();
    ImGui::Text("%s", labelBuilder.str().c_str());
    ImGui::SameLine();

    {
        ImGuiExtensions::ButtonColorChanger cc(ImGuiExtensions::ButtonColor::Red);
        if (ImGui::SmallButton("Close device"))
        {
            K4AWindowManager::Instance().PopDockControl();
            return;
        }
    }

    ImGui::Separator();

    const bool deviceIsStarted = DeviceIsStarted();

    if (!m_paused)
    {
        PollDevice();
    }

    // Draw controls
    //
    // InputScalars are a bit wider than we want them by default.
    //
    constexpr float InputScalarScaleFactor = 0.5f;

    bool depthEnabledStateChanged = ImGuiExtensions::K4ACheckbox("Enable Depth Camera",
                                                                 &m_pendingDeviceConfiguration.EnableDepthCamera,
                                                                 !deviceIsStarted);

    if (m_firstRun || depthEnabledStateChanged)
    {
        ImGui::SetNextTreeNodeOpen(m_pendingDeviceConfiguration.EnableDepthCamera);
    }

    ImGui::Indent();
    if (ImGui::TreeNode("Depth Configuration"))
    {
        const bool depthSettingsEditable = !deviceIsStarted && m_pendingDeviceConfiguration.EnableDepthCamera;
        bool depthModeUpdated = depthEnabledStateChanged;
        auto *pDepthMode = reinterpret_cast<int *>(&m_pendingDeviceConfiguration.DepthMode);
        ImGui::Text("Depth mode");
        depthModeUpdated |= ImGuiExtensions::K4ARadioButton("NFOV Binned",
                                                            pDepthMode,
                                                            K4A_DEPTH_MODE_NFOV_2X2BINNED,
                                                            depthSettingsEditable);
        ImGui::SameLine();
        depthModeUpdated |= ImGuiExtensions::K4ARadioButton("NFOV Unbinned  ",
                                                            pDepthMode,
                                                            K4A_DEPTH_MODE_NFOV_UNBINNED,
                                                            depthSettingsEditable);
        // New line
        depthModeUpdated |= ImGuiExtensions::K4ARadioButton("WFOV Binned",
                                                            pDepthMode,
                                                            K4A_DEPTH_MODE_WFOV_2X2BINNED,
                                                            depthSettingsEditable);
        ImGui::SameLine();
        depthModeUpdated |= ImGuiExtensions::K4ARadioButton("WFOV Unbinned  ",
                                                            pDepthMode,
                                                            K4A_DEPTH_MODE_WFOV_UNBINNED,
                                                            depthSettingsEditable);
        // New line
        depthModeUpdated |=
            ImGuiExtensions::K4ARadioButton("Passive IR", pDepthMode, K4A_DEPTH_MODE_PASSIVE_IR, depthSettingsEditable);

        if (depthModeUpdated || m_firstRun)
        {
            if (m_pendingDeviceConfiguration.DepthMode == K4A_DEPTH_MODE_WFOV_UNBINNED)
            {
                m_pendingDeviceConfiguration.Framerate = K4A_FRAMES_PER_SECOND_15;
            }
        }

        ImGui::TreePop();
    }
    ImGui::Unindent();

    bool colorEnableStateChanged = ImGuiExtensions::K4ACheckbox("Enable Color Camera",
                                                                &m_pendingDeviceConfiguration.EnableColorCamera,
                                                                !deviceIsStarted);

    if (m_firstRun || colorEnableStateChanged)
    {
        ImGui::SetNextTreeNodeOpen(m_pendingDeviceConfiguration.EnableColorCamera);
    }

    ImGui::Indent();
    if (ImGui::TreeNode("Color Configuration"))
    {
        const bool colorSettingsEditable = !deviceIsStarted && m_pendingDeviceConfiguration.EnableColorCamera;

        bool colorFormatUpdated = false;
        auto *pColorFormat = reinterpret_cast<int *>(&m_pendingDeviceConfiguration.ColorFormat);
        ImGui::Text("Format");
        colorFormatUpdated |=
            ImGuiExtensions::K4ARadioButton("MJPG", pColorFormat, K4A_IMAGE_FORMAT_COLOR_MJPG, colorSettingsEditable);
        ImGui::SameLine();
        colorFormatUpdated |=
            ImGuiExtensions::K4ARadioButton("BGRA", pColorFormat, K4A_IMAGE_FORMAT_COLOR_BGRA32, colorSettingsEditable);
        ImGui::SameLine();
        colorFormatUpdated |=
            ImGuiExtensions::K4ARadioButton("NV12", pColorFormat, K4A_IMAGE_FORMAT_COLOR_NV12, colorSettingsEditable);
        ImGui::SameLine();
        colorFormatUpdated |=
            ImGuiExtensions::K4ARadioButton("YUY2", pColorFormat, K4A_IMAGE_FORMAT_COLOR_YUY2, colorSettingsEditable);

        // Uncompressed formats are only supported at 720p.
        //
        const bool imageFormatSupportsHighResolution = m_pendingDeviceConfiguration.ColorFormat !=
                                                           K4A_IMAGE_FORMAT_COLOR_NV12 &&
                                                       m_pendingDeviceConfiguration.ColorFormat !=
                                                           K4A_IMAGE_FORMAT_COLOR_YUY2;
        if (colorFormatUpdated || m_firstRun)
        {
            if (!imageFormatSupportsHighResolution)
            {
                m_pendingDeviceConfiguration.ColorResolution = K4A_COLOR_RESOLUTION_720P;
            }
        }

        bool colorResolutionUpdated = colorEnableStateChanged;
        auto *pColorResolution = reinterpret_cast<int *>(&m_pendingDeviceConfiguration.ColorResolution);

        ImGui::Text("Resolution");
        ImGui::Indent();
        ImGui::Text("16:9");
        ImGui::Indent();
        colorResolutionUpdated |= ImGuiExtensions::K4ARadioButton(" 720p",
                                                                  pColorResolution,
                                                                  K4A_COLOR_RESOLUTION_720P,
                                                                  colorSettingsEditable);
        ImGui::SameLine();
        colorResolutionUpdated |= ImGuiExtensions::K4ARadioButton("1080p",
                                                                  pColorResolution,
                                                                  K4A_COLOR_RESOLUTION_1080P,
                                                                  colorSettingsEditable &&
                                                                      imageFormatSupportsHighResolution);
        // New line
        colorResolutionUpdated |= ImGuiExtensions::K4ARadioButton("1440p",
                                                                  pColorResolution,
                                                                  K4A_COLOR_RESOLUTION_1440P,
                                                                  colorSettingsEditable &&
                                                                      imageFormatSupportsHighResolution);
        ImGui::SameLine();
        colorResolutionUpdated |= ImGuiExtensions::K4ARadioButton("2160p",
                                                                  pColorResolution,
                                                                  K4A_COLOR_RESOLUTION_2160P,
                                                                  colorSettingsEditable &&
                                                                      imageFormatSupportsHighResolution);
        ImGui::Unindent();
        ImGui::Text("4:3");
        ImGui::Indent();

        colorResolutionUpdated |= ImGuiExtensions::K4ARadioButton("1536p",
                                                                  pColorResolution,
                                                                  K4A_COLOR_RESOLUTION_1536P,
                                                                  colorSettingsEditable &&
                                                                      imageFormatSupportsHighResolution);

        ImGui::SameLine();
        colorResolutionUpdated |= ImGuiExtensions::K4ARadioButton("3072p",
                                                                  pColorResolution,
                                                                  K4A_COLOR_RESOLUTION_3072P,
                                                                  colorSettingsEditable &&
                                                                      imageFormatSupportsHighResolution);

        ImGui::Unindent();
        ImGui::Unindent();

        if (colorResolutionUpdated || m_firstRun)
        {
            if (m_pendingDeviceConfiguration.ColorResolution == K4A_COLOR_RESOLUTION_3072P)
            {
                // 4K supports up to 15FPS
                //
                m_pendingDeviceConfiguration.Framerate = K4A_FRAMES_PER_SECOND_15;
            }
        }

        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Color Controls"))
    {
        // Some of the variable names in here are just long enough to cause clang-format to force a line-wrap,
        // which causes half of these (very similar) calls to look radically different than the rest, which
        // makes it harder to pick out similarities/differences at a glance, so turn off clang-format for this
        // bit.
        //
        // clang-format off

        const float SliderScaleFactor = 0.5f;

        ShowColorControl(K4A_COLOR_CONTROL_EXPOSURE_TIME_ABSOLUTE, m_colorSettingsCache.ExposureTimeUs,
            [SliderScaleFactor](ColorSetting &cacheEntry) {
                ColorControlAction result = ColorControlAction::None;

                // Exposure time supported values are factors off 1,000,000 / 2, so we need an exponential control.
                // There isn't one for ints, so we use the float control and make it look like an int control.
                //
                auto valueFloat = static_cast<float>(cacheEntry.Value);
                ImGui::PushItemWidth(ImGui::CalcItemWidth() * SliderScaleFactor);
                if (ImGuiExtensions::K4ASliderFloat("Exposure Time",
                                                    &valueFloat,
                                                    488.f,
                                                    1000000.f,
                                                    "%.0f us",
                                                    8.0f,
                                                    cacheEntry.Mode == K4A_COLOR_CONTROL_MODE_MANUAL))
                {
                    result = ColorControlAction::SetManual;
                    cacheEntry.Value = static_cast<int32_t>(valueFloat);
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();
                ShowColorControlAutoButton(cacheEntry.Mode, result, "exposure");
                return result;
        });

        ShowColorControl(K4A_COLOR_CONTROL_WHITEBALANCE, m_colorSettingsCache.WhiteBalance,
            [SliderScaleFactor](ColorSetting &cacheEntry) {
                ColorControlAction result = ColorControlAction::None;
                ImGui::PushItemWidth(ImGui::CalcItemWidth() * SliderScaleFactor);
                if (ImGuiExtensions::K4ASliderInt("White Balance",
                                                  &cacheEntry.Value,
                                                  2500,
                                                  12500,
                                                  "%d K",
                                                  cacheEntry.Mode == K4A_COLOR_CONTROL_MODE_MANUAL))
                {
                    result = ColorControlAction::SetManual;

                    // White balance must be stepped in units of 10 or the call to update the setting fails.
                    //
                    cacheEntry.Value -= cacheEntry.Value % 10;
                }
                ImGui::PopItemWidth();

                ImGui::SameLine();
                ShowColorControlAutoButton(cacheEntry.Mode, result, "whitebalance");
                return result;
        });

        ImGui::PushItemWidth(ImGui::CalcItemWidth() * SliderScaleFactor);

        ShowColorControl(K4A_COLOR_CONTROL_BRIGHTNESS, m_colorSettingsCache.Brightness,
            [](ColorSetting &cacheEntry) {
                return ImGui::SliderInt("Brightness", &cacheEntry.Value, 0, 255) ?
                    ColorControlAction::SetManual :
                    ColorControlAction::None;
        });

        ShowColorControl(K4A_COLOR_CONTROL_CONTRAST, m_colorSettingsCache.Contrast,
            [](ColorSetting &cacheEntry) {
                return ImGui::SliderInt("Contrast", &cacheEntry.Value, 0, 10) ?
                    ColorControlAction::SetManual :
                    ColorControlAction::None;
        });

        ShowColorControl(K4A_COLOR_CONTROL_SATURATION, m_colorSettingsCache.Saturation,
            [](ColorSetting &cacheEntry) {
                return ImGui::SliderInt("Saturation", &cacheEntry.Value, 0, 63) ?
                    ColorControlAction::SetManual :
                    ColorControlAction::None;
        });

        ShowColorControl(K4A_COLOR_CONTROL_SHARPNESS, m_colorSettingsCache.Sharpness,
            [](ColorSetting &cacheEntry) {
                return ImGui::SliderInt("Sharpness", &cacheEntry.Value, 0, 4) ?
                    ColorControlAction::SetManual :
                    ColorControlAction::None;
        });

        ShowColorControl(K4A_COLOR_CONTROL_GAIN, m_colorSettingsCache.Gain,
            [](ColorSetting &cacheEntry) {
                return ImGui::SliderInt("Gain", &cacheEntry.Value, 0, 255) ?
                    ColorControlAction::SetManual :
                    ColorControlAction::None;
        });

        ImGui::PopItemWidth();

        ShowColorControl(K4A_COLOR_CONTROL_AUTO_EXPOSURE_PRIORITY, m_colorSettingsCache.AutoExposurePriority,
            [](ColorSetting &cacheEntry) {
                return ImGui::Checkbox("Auto Exposure Priority", reinterpret_cast<bool *>(&cacheEntry.Value)) ?
                    ColorControlAction::SetManual :
                    ColorControlAction::None;
         });

        ShowColorControl(K4A_COLOR_CONTROL_BACKLIGHT_COMPENSATION, m_colorSettingsCache.BacklightCompensation,
            [](ColorSetting &cacheEntry) {
                return ImGui::Checkbox("Backlight Compensation", reinterpret_cast<bool *>(&cacheEntry.Value)) ?
                    ColorControlAction::SetManual :
                    ColorControlAction::None;
         });

        ShowColorControl(K4A_COLOR_CONTROL_POWERLINE_FREQUENCY, m_colorSettingsCache.PowerlineFrequency,
            [](ColorSetting &cacheEntry) {
                ImGui::Text("Power Frequency");
                ImGui::SameLine();
                bool updated = false;
                updated |= ImGui::RadioButton("50Hz", &cacheEntry.Value, 1);
                ImGui::SameLine();
                updated |= ImGui::RadioButton("60Hz", &cacheEntry.Value, 2);
                return updated ? ColorControlAction::SetManual : ColorControlAction::None;
         });

        // clang-format on

        if (ImGui::Button("Reset to default##RGB"))
        {
            ApplyDefaultColorSettings();
        }

        ImGui::TreePop();
    }
    ImGui::Unindent();

    const bool supports30fps = !(m_pendingDeviceConfiguration.EnableColorCamera &&
                                 m_pendingDeviceConfiguration.ColorResolution == K4A_COLOR_RESOLUTION_3072P) &&
                               !(m_pendingDeviceConfiguration.EnableDepthCamera &&
                                 m_pendingDeviceConfiguration.DepthMode == K4A_DEPTH_MODE_WFOV_UNBINNED);

    const bool enableFramerate = !deviceIsStarted && (m_pendingDeviceConfiguration.EnableColorCamera ||
                                                      m_pendingDeviceConfiguration.EnableDepthCamera);

    ImGui::Text("Framerate");
    auto *pFramerate = reinterpret_cast<int *>(&m_pendingDeviceConfiguration.Framerate);
    ImGuiExtensions::K4ARadioButton("30 FPS", pFramerate, K4A_FRAMES_PER_SECOND_30, enableFramerate && supports30fps);
    ImGui::SameLine();
    ImGuiExtensions::K4ARadioButton("15 FPS", pFramerate, K4A_FRAMES_PER_SECOND_15, enableFramerate);
    ImGui::SameLine();
    ImGuiExtensions::K4ARadioButton(" 5 FPS", pFramerate, K4A_FRAMES_PER_SECOND_5, enableFramerate);

    ImGuiExtensions::K4ACheckbox("Disable streaming LED",
                                 &m_pendingDeviceConfiguration.DisableStreamingIndicator,
                                 !deviceIsStarted);

    ImGui::Separator();

    ImGuiExtensions::K4ACheckbox("Enable IMU", &m_pendingDeviceConfiguration.EnableImu, !deviceIsStarted);

    const bool synchronizedImagesAvailable = m_pendingDeviceConfiguration.EnableColorCamera &&
                                             m_pendingDeviceConfiguration.EnableDepthCamera;
    m_pendingDeviceConfiguration.SynchronizedImagesOnly &= synchronizedImagesAvailable;

    if (m_microphone)
    {
        ImGuiExtensions::K4ACheckbox("Enable Microphone",
                                     &m_pendingDeviceConfiguration.EnableMicrophone,
                                     !deviceIsStarted);
    }
    else
    {
        m_pendingDeviceConfiguration.EnableMicrophone = false;
        ImGui::Text("Microphone not detected!");
    }

    ImGui::Separator();

    if (ImGui::TreeNode("Internal Sync"))
    {
        ImGuiExtensions::K4ACheckbox("Synchronized images only",
                                     &m_pendingDeviceConfiguration.SynchronizedImagesOnly,
                                     !deviceIsStarted && synchronizedImagesAvailable);

        ImGui::PushItemWidth(ImGui::CalcItemWidth() * InputScalarScaleFactor);
        constexpr int stepSize = 1;
        ImGuiExtensions::K4AInputScalar("Depth delay (us)",
                                        ImGuiDataType_S32,
                                        &m_pendingDeviceConfiguration.DepthDelayOffColorUsec,
                                        &stepSize,
                                        nullptr,
                                        "%d",
                                        !deviceIsStarted);
        ImGui::PopItemWidth();
        ImGui::TreePop();
    }

    if (m_firstRun && (m_syncInConnected || m_syncOutConnected))
    {
        ImGui::SetNextTreeNodeOpen(true);
    }
    if (ImGui::TreeNode("External Sync"))
    {
        ImGui::Text("Sync cable state");
        ImGuiExtensions::K4ARadioButton("In", m_syncInConnected, false);
        ImGui::SameLine();
        ImGuiExtensions::K4ARadioButton("Out", m_syncOutConnected, false);
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            RefreshSyncCableStatus();
        }

        const bool syncModesSupported = (m_syncInConnected || m_syncOutConnected) &&
                                        (m_pendingDeviceConfiguration.EnableColorCamera ||
                                         m_pendingDeviceConfiguration.EnableDepthCamera);
        if (!syncModesSupported)
        {
            m_pendingDeviceConfiguration.WiredSyncMode = K4A_WIRED_SYNC_MODE_STANDALONE;
        }

        auto *pSyncMode = reinterpret_cast<int *>(&m_pendingDeviceConfiguration.WiredSyncMode);
        ImGuiExtensions::K4ARadioButton("Standalone", pSyncMode, K4A_WIRED_SYNC_MODE_STANDALONE, !deviceIsStarted);
        ImGui::SameLine();
        ImGuiExtensions::K4ARadioButton("Master",
                                        pSyncMode,
                                        K4A_WIRED_SYNC_MODE_MASTER,
                                        !deviceIsStarted && syncModesSupported);
        ImGui::SameLine();
        ImGuiExtensions::K4ARadioButton("Sub",
                                        pSyncMode,
                                        K4A_WIRED_SYNC_MODE_SUBORDINATE,
                                        !deviceIsStarted && syncModesSupported);

        constexpr int stepSize = 1;
        ImGui::PushItemWidth(ImGui::CalcItemWidth() * InputScalarScaleFactor);
        ImGuiExtensions::K4AInputScalar("Delay off master (us)",
                                        ImGuiDataType_U32,
                                        &m_pendingDeviceConfiguration.SubordinateDelayOffMasterUsec,
                                        &stepSize,
                                        nullptr,
                                        "%d",
                                        !deviceIsStarted);
        ImGui::PopItemWidth();

        ImGui::TreePop();
    }

    ImGui::Separator();

    if (ImGui::TreeNode("Device Firmware Version Info"))
    {
        const k4a_hardware_version_t &versionInfo = m_device->GetVersionInfo();
        ImGui::Text("RGB camera: %u.%u.%u", versionInfo.rgb.major, versionInfo.rgb.minor, versionInfo.rgb.iteration);
        ImGui::Text("Depth camera: %u.%u.%u",
                    versionInfo.depth.major,
                    versionInfo.depth.minor,
                    versionInfo.depth.iteration);
        ImGui::Text("Audio: %u.%u.%u", versionInfo.audio.major, versionInfo.audio.minor, versionInfo.audio.iteration);

        ImGui::Text("Build Config: %s", versionInfo.firmware_build == K4A_FIRMWARE_BUILD_RELEASE ? "Release" : "Debug");
        ImGui::Text("Signature type: %s",
                    versionInfo.firmware_signature == K4A_FIRMWARE_SIGNATURE_MSFT ?
                        "Microsoft" :
                        versionInfo.firmware_signature == K4A_FIRMWARE_SIGNATURE_TEST ? "Test" : "Unsigned");

        ImGui::TreePop();
    }

    ImGui::Separator();

    if (ImGuiExtensions::K4AButton("Restore", !deviceIsStarted))
        ApplyDefaultConfiguration();
    ImGui::SameLine();
    if (ImGuiExtensions::K4AButton("Save", !deviceIsStarted))
        SaveDefaultConfiguration();
    ImGui::SameLine();
    if (ImGuiExtensions::K4AButton("Reset", !deviceIsStarted))
        ResetDefaultConfiguration();

    const bool enableCameras = m_pendingDeviceConfiguration.EnableColorCamera ||
                               m_pendingDeviceConfiguration.EnableDepthCamera;

    const ImVec2 buttonSize{ 275, 0 };
    if (!deviceIsStarted)
    {
        ImGuiExtensions::ButtonColorChanger colorChanger(ImGuiExtensions::ButtonColor::Green);
        const bool validStartMode = enableCameras || m_pendingDeviceConfiguration.EnableMicrophone ||
                                    m_pendingDeviceConfiguration.EnableImu;
        if (ImGuiExtensions::K4AButton("Start", buttonSize, validStartMode))
        {
            Start();
        }
    }
    else
    {
        ImGuiExtensions::ButtonColorChanger colorChanger(ImGuiExtensions::ButtonColor::Red);
        if (ImGuiExtensions::K4AButton("Stop", buttonSize))
        {
            Stop();
        }

        ImGui::Separator();

        const bool pointCloudViewerAvailable = m_pendingDeviceConfiguration.EnableDepthCamera &&
                                               m_pendingDeviceConfiguration.DepthMode != K4A_DEPTH_MODE_PASSIVE_IR &&
                                               m_device->CamerasAreStarted();

        K4AWindowSet::ShowModeSelector(&m_currentViewType,
                                       true,
                                       pointCloudViewerAvailable,
                                       [this](K4AWindowSet::ViewType t) { return this->SetViewType(t); });

        if (m_paused)
        {
            ImGuiExtensions::ButtonColorChanger cc(ImGuiExtensions::ButtonColor::Green);
            if (ImGui::Button("Resume", buttonSize))
            {
                m_paused = false;
            }
        }
        else
        {
            ImGuiExtensions::ButtonColorChanger cc(ImGuiExtensions::ButtonColor::Yellow);
            if (ImGui::Button("Pause", buttonSize))
            {
                m_paused = true;
            }
        }
    }

    m_firstRun = false;
}

void K4ADeviceDockControl::PollDevice()
{
    if (m_device->CamerasAreStarted())
    {
        PollSensor<std::shared_ptr<K4ACapture>>(*m_device,
                                                "Cameras",
                                                [](K4ADevice &device, std::shared_ptr<K4ACapture> &result) {
                                                    std::unique_ptr<K4ACapture> p;
                                                    k4a_wait_result_t status = device.PollCameras(p);
                                                    result = std::move(p);
                                                    return status;
                                                },
                                                [](K4ADevice &device) { device.StopCameras(); },
                                                m_cameraTimeoutCounter,
                                                m_cameraDataSource);
    }

    if (m_device->ImuIsStarted())
    {
        PollSensor<k4a_imu_sample_t>(*m_device,
                                     "IMU",
                                     [](K4ADevice &device, k4a_imu_sample_t &result) {
                                         // The IMU refreshes significantly faster than the viewer's
                                         // framerate, so if we only grab one IMU sample per app frame,
                                         // we end up dropping a bunch of IMU samples, which generates
                                         // a bunch of noise in the K4A SDK logs.
                                         //
                                         // To mitigate this, we drain the queue whenever we poll the
                                         // IMU.  This is reasonable for a viewer app where we're just
                                         // trying to see if the IMU is returning data, but not for real
                                         // IMU applications like tracking where you can't afford to drop
                                         // samples.
                                         //
                                         // TODO move device polling to a separate thread so we can
                                         //      decouple the IMU polling rate from the app framerate.
                                         //
                                         bool gotSample = false;
                                         k4a_wait_result_t status;
                                         while (K4A_WAIT_RESULT_SUCCEEDED == (status = device.PollImu(result)))
                                         {
                                             gotSample = true;
                                         }
                                         if (gotSample && status == K4A_WAIT_RESULT_TIMEOUT)
                                         {
                                             status = K4A_WAIT_RESULT_SUCCEEDED;
                                         }
                                         return status;
                                     },
                                     [](K4ADevice &device) { device.StopImu(); },
                                     m_imuTimeoutCounter,
                                     m_imuDataSource);
    }

    if (m_microphone && m_microphone->GetStatusCode() != SoundIoErrorNone)
    {
        std::stringstream errorBuilder;
        errorBuilder << "Microphone on device " << m_device->GetSerialNumber() << " failed!";
        K4AViewerErrorManager::Instance().SetErrorStatus(errorBuilder.str());
        StopMicrophone();
    }
}

void K4ADeviceDockControl::Start()
{
    const bool enableCameras = m_pendingDeviceConfiguration.EnableColorCamera ||
                               m_pendingDeviceConfiguration.EnableDepthCamera;
    if (enableCameras)
    {
        StartCameras();
    }
    if (m_pendingDeviceConfiguration.EnableImu)
    {
        StartImu();
    }
    if (m_pendingDeviceConfiguration.EnableMicrophone)
    {
        StartMicrophone();
    }

    SetViewType(K4AWindowSet::ViewType::Normal);
    m_paused = false;
}

void K4ADeviceDockControl::Stop()
{
    K4AWindowManager::Instance().ClearWindows();

    m_device->StopCameras();
    m_device->StopImu();
    StopMicrophone();
}

bool K4ADeviceDockControl::StartCameras()
{
    if (m_device->CamerasAreStarted())
    {
        return false;
    }

    k4a_device_configuration_t deviceConfig = m_pendingDeviceConfiguration.ToK4ADeviceConfiguration();

    const k4a_result_t result = m_device->StartCameras(deviceConfig);
    if (result != K4A_RESULT_SUCCEEDED)
    {
        K4AViewerErrorManager::Instance().SetErrorStatus(
            "Failed to start device!\nIf you unplugged the device, you must close and reopen the device.");
        return false;
    }

    return true;
}

bool K4ADeviceDockControl::StartMicrophone()
{
    if (!m_microphone)
    {
        std::stringstream errorBuilder;
        errorBuilder << "Failed to find microphone for device: " << m_device->GetSerialNumber() << "!";
        K4AViewerErrorManager::Instance().SetErrorStatus(errorBuilder.str());
        return false;
    }

    if (m_microphone->IsStarted())
    {
        return false;
    }

    const int startResult = m_microphone->Start();
    if (startResult != SoundIoErrorNone)
    {
        std::stringstream errorBuilder;
        errorBuilder << "Failed to start microphone: " << soundio_strerror(startResult) << "!";
        K4AViewerErrorManager::Instance().SetErrorStatus(errorBuilder.str());
        return false;
    }

    return true;
}

void K4ADeviceDockControl::StopMicrophone()
{
    if (m_microphone)
    {
        m_microphone->Stop();
    }
}

bool K4ADeviceDockControl::StartImu()
{
    if (m_device->ImuIsStarted())
    {
        return false;
    }

    const k4a_result_t startResult = m_device->StartImu();

    if (startResult != K4A_RESULT_SUCCEEDED)
    {
        K4AViewerErrorManager::Instance().SetErrorStatus("Failed to start IMU!");
        return false;
    }

    return true;
}

void K4ADeviceDockControl::SetViewType(K4AWindowSet::ViewType viewType)
{
    K4AWindowManager::Instance().ClearWindows();
    switch (viewType)
    {
    case K4AWindowSet::ViewType::Normal:
        K4AWindowSet::StartNormalWindows(m_device->GetSerialNumber().c_str(),
                                         &m_cameraDataSource,
                                         &m_imuDataSource,
                                         m_pendingDeviceConfiguration.EnableMicrophone ?
                                             m_microphone->CreateListener() :
                                             nullptr,
                                         m_pendingDeviceConfiguration.EnableDepthCamera,
                                         m_pendingDeviceConfiguration.DepthMode,
                                         m_pendingDeviceConfiguration.EnableColorCamera,
                                         m_pendingDeviceConfiguration.ColorFormat,
                                         m_pendingDeviceConfiguration.ColorResolution);
        break;

    case K4AWindowSet::ViewType::PointCloudViewer:
        std::unique_ptr<K4ACalibrationTransformData> calibrationData;

        const k4a_result_t getCalibrationResult =
            m_device->GetCalibrationTransformData(calibrationData,
                                                  m_pendingDeviceConfiguration.DepthMode,
                                                  m_pendingDeviceConfiguration.ColorResolution);

        if (getCalibrationResult != K4A_RESULT_SUCCEEDED)
        {
            K4AViewerErrorManager::Instance().SetErrorStatus("Failed to get calibration data!");
            return;
        }

        K4AWindowSet::StartPointCloudWindow(m_device->GetSerialNumber().c_str(),
                                            std::move(calibrationData),
                                            m_cameraDataSource,
                                            m_pendingDeviceConfiguration.DepthMode);
        break;
    }

    m_currentViewType = viewType;
}

void K4ADeviceDockControl::CloseDevice()
{
    if (m_device)
    {
        Stop();
        m_device.reset();
    }
}

void K4ADeviceDockControl::ApplyDefaultConfiguration()
{
    m_pendingDeviceConfiguration = K4AViewerSettingsManager::Instance().GetSavedDeviceConfiguration();
}

void K4ADeviceDockControl::SaveDefaultConfiguration()
{
    K4AViewerSettingsManager::Instance().SetSavedDeviceConfiguration(m_pendingDeviceConfiguration);
}

void K4ADeviceDockControl::ResetDefaultConfiguration()
{
    m_pendingDeviceConfiguration = K4ADeviceConfiguration();
    SaveDefaultConfiguration();
}