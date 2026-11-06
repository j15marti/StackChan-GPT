#include "stackchan_camera.h"
#include "assets/assets.h"
#include "hal_bridge.h"

bool StackChanCamera::Capture()
{
    hal_bridge::app_play_sound(OGG_CAMERA_SHUTTER);
    return EspVideo::Capture();
}

bool StackChanCamera::StreamCaptures(int discard_frames)
{
    return CaptureFrame(false, discard_frames);
}
