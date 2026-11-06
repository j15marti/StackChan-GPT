# StackChan-GPT v1.8.7: deterministic idle camera tracking

Version 1.8.7 adds local motion tracking for the CoreS3 camera and StackChan head servos. The
feature is deterministic. It does not invoke OpenAI, another AI model, or a network service.

## Runtime behavior

1. The tracker remains disabled while StackChan is starting, listening, responding, playing audio,
   showing a notification, being touched, or running another motion modifier.
2. After the device has remained at rest for 30 seconds, the first motion check runs. Additional
   idle checks run every 10 seconds.
3. A check captures two raw camera frames 250 ms apart without the camera preview or shutter sound.
4. Frames are converted to full-resolution luma. The median frame-wide brightness change is removed
   before individual pixels are compared. Median compensation prevents a uniform exposure shift from
   being treated as scene motion without letting a large moving object distort the correction.
5. A pixel is marked as changed when its compensated luma difference is at least 24 on the 0 to 255
   scale. The image is split into four quadrants. Tracking starts only if strictly more than 100 per
   1,000 pixels, equivalent to more than 10%, change in at least one quadrant.
6. The quadrant with the highest changed-pixel ratio selects the main region. The centroid of all
   changed pixels supplies the directional correction so movement crossing the frame center can be
   followed without becoming trapped at a quadrant boundary.
7. While tracking, the firmware checks motion at approximately one-second intervals. Corrections use
   a 12% center deadband, a maximum yaw step of 32 degrees, a maximum pitch step of 22 degrees, and a
   natural servo speed of 220. Tracking stays within yaw -100 to +100 degrees and pitch 5 to 80
   degrees.
8. A tracking session lasts no more than 30 seconds. At the time limit the current tracking movement
   is stopped and the dedicated servo lock is released. The ordinary idle behavior then resumes.

The task polls for cancellation every 50 ms. A new listening, speaking, notification, playback, or
touch state prevents any further tracking correction. The tracker also waits for its own servo motion
to settle before taking the next frame pair so camera motion caused by the head itself is not mistaken
for movement in the room.

## Privacy and resource behavior

- Raw frames are held only in volatile memory for the comparison.
- No frame is saved to flash or microSD.
- No image is uploaded or added to conversation history.
- No API key, Wi-Fi credential, or user configuration is embedded in the release binary.
- Camera capture uses the existing camera mutex and skips a scan if another camera operation is active.

## Automated validation

The host test suite covers the strict 10% boundary, dominant-quadrant selection, changed-pixel
centroid, uniform brightness rejection, packed YUYV and UYVY conversion, RGB565 conversion, servo
direction, center deadband, and angle clamping. The complete ESP-IDF firmware is also built before the
release image is published.

Release validation results:

- Host tests: 3 of 3 passed, including the idle camera motion tests.
- ESP-IDF: v6.1, target ESP32-S3, DIO, 80 MHz, 16 MB flash.
- Application image: 3,952,832 bytes with 24% of the smallest application partition free.
- Merged image: 12,857,054 bytes.
- Merged image SHA-256: `1720be5b9d7c483dbfea0ebfdc3a310f883bb4c55709abacf370010100dc69ea`.
- Image validation checksum and hash: valid.

The build and deterministic analysis were validated in the development environment. Physical servo
direction, camera sensitivity, and cancellation timing still require the post-flash hardware checks
below because no StackChan unit was connected to the build environment.

To run the host tests after dependencies have been downloaded:

```bash
cd firmware
cmake -S tests -B build-host -DCJSON_DIR="$PWD/managed_components/espressif__cjson/cJSON"
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

To build and merge the firmware with ESP-IDF 6.1:

```bash
cd firmware
source ~/esp-idf/export.sh
idf.py build
idf.py merge-bin -o ../../StackChan-GPT-v1.8.7.bin
```

## Flashing

`StackChan-GPT-v1.8.7.bin` is a merged image for the 16 MB ESP32-S3 StackChan and must be flashed at
address `0x0`:

```bash
esptool.py --chip esp32s3 -p /dev/cu.usbmodem* -b 460800 write_flash 0x0 StackChan-GPT-v1.8.7.bin
```

Keep a current microSD configuration backup before writing a merged image. Its padding overwrites the
NVS settings area. This is not an OTA application-only image.

## Hardware checks after flashing

1. Leave StackChan idle for 30 seconds, then move an object through more than 10% of one camera
   quadrant. Confirm the head turns toward the moving region.
2. Continue moving the object and confirm the head makes approximately one correction per second.
3. Speak the wake word or tap the screen during tracking. Confirm tracking stops and the assistant
   enters its normal interaction state.
4. Allow a tracking session to continue and confirm it stops by 30 seconds.
5. Change room lighting uniformly without moving an object. Confirm ordinary brightness changes do
   not normally trigger tracking.

Thresholds are fixed in firmware for this release. Shadows, flickering lights, camera noise, or large
display reflections can still resemble pixel motion, so final sensitivity should be evaluated on the
physical robot in its usual lighting.
