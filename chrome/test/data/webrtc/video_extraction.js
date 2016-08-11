/**
 * Copyright (c) 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * The gStartedAt when the capturing begins. Used for timeout adjustments.
 * @private
 */
var gStartedAt = 0;

/**
 * The duration of the all frame capture in milliseconds.
 * @private
 */
var gCaptureDuration = 0;

/**
 * The time interval at which the video is sampled.
 * @private
 */
var gFrameCaptureInterval = 0;

/**
 * The global array of frames. Frames are pushed, i.e. this should be treated as
 * a queue and we should read from the start.
 * @private
 */
var gFrames = [];

/**
 * We need to skip the first two frames due to timing issues.
 * @private
 */
var gHasThrownAwayFirstTwoFrames = false;

/**
 * A string to be returned to the test about the current status of capture.
 */
var gCapturingStatus = 'capturing-not-started';

/**
 * Starts the frame capturing.
 *
 * @param {!Object} The video tag from which the height and width parameters are
                    to be extracted.
 * @param {Number} The frame rate at which we would like to capture frames.
 * @param {Number} The duration of the frame capture in seconds.
 */
function startFrameCapture(videoTag, frameRate, duration) {
  gFrameCaptureInterval = 1000 / frameRate;
  gCaptureDuration = 1000 * duration;
  inputElement = document.getElementById("local-view");
  var width = inputElement.videoWidth;
  var height = inputElement.videoHeight;

  // The WebRTC code is free to start in VGA, so make sure that the output video
  // tag scales up to whatever the input size is (otherwise the video quality
  // comparison will go poorly.
  videoTag.width = width;
  videoTag.height = height;

  if (width == 0 || height == 0) {
    // Video must be playing at this point since this function is invoked from
    // onplay on the <video> tag. See http://crbug.com/625943.
    gCapturingStatus = 'failed-video-was-0x0-after-onplay'
    return;
  }

  console.log('Received width is: ' + width + ', received height is: ' + height
              + ', capture interval is: ' + gFrameCaptureInterval +
              ', duration is: ' + gCaptureDuration);
  gCapturingStatus = 'still-capturing';

  var remoteCanvas = document.createElement('canvas');
  remoteCanvas.width = width;
  remoteCanvas.height = height;
  document.body.appendChild(remoteCanvas);

  gStartedAt = new Date().getTime();
  gFrames = [];
  setTimeout(function() { shoot_(videoTag, remoteCanvas, width, height); },
             gFrameCaptureInterval);
}

/**
 * Queries if we're done with the frame capturing yet.
 */
function doneFrameCapturing() {
  returnToTest(gCapturingStatus);
}

/**
 * Retrieves the number of captured frames.
 */
function getTotalNumberCapturedFrames() {
  returnToTest(gFrames.length.toString());
}

/**
 * Retrieves one captured frame in ARGB format as a base64-encoded string.
 *
 * Also updates the page's progress bar.
 *
 * @param frameIndex A frame index in the range 0 to total-1 where total is
 *     given by getTotalNumberCapturedFrames.
 */
function getOneCapturedFrame(frameIndex) {
  var codedFrame = convertArrayBufferToBase64String_(gFrames[frameIndex]);
  updateProgressBar_(frameIndex);
  silentReturnToTest(codedFrame);
}

/**
 * @private
 *
 * @param {ArrayBuffer} buffer An array buffer to convert to a base 64 string.
 * @return {String} A base 64 string.
 */
function convertArrayBufferToBase64String_(buffer) {
  var binary = '';
  var bytes = new Uint8Array(buffer);
  for (var i = 0; i < bytes.byteLength; i++) {
    binary += String.fromCharCode(bytes[i]);
  }
  return window.btoa(binary);
}

/**
 * The function which is called at the end of every gFrameCaptureInterval. Gets
 * the current frame from the video and extracts the data from it. Then it saves
 * it in the frames array and adjusts the capture interval (timers in JavaScript
 * aren't precise).
 *
 * @private
 *
 * @param {!Object} The video whose frames are to be captured.
 * @param {Canvas} The canvas on which the image will be captured.
 * @param {Number} The width of the video/canvas area to be captured.
 * @param {Number} The height of the video area to be captured.
 */
function shoot_(video, canvas, width, height) {
  // The first two captured frames have big difference between the ideal time
  // interval between two frames and the real one. As a consequence this affects
  // enormously the interval adjustment for subsequent frames. That's why we
  // have to reset the time after the first two frames and get rid of these two
  // frames.
  if (gFrames.length == 1 && !gHasThrownAwayFirstTwoFrames) {
    gStartedAt = new Date().getTime();
    gHasThrownAwayFirstTwoFrames = true;
    gFrames = [];
  }

  // We capture the whole video frame.
  var img = captureFrame_(video, canvas.getContext('2d'), width, height);
  gFrames.push(img.data.buffer);

  // Adjust the timer and try to account for timer incorrectness.
  var currentTime = new Date().getTime();
  var idealTime = gFrames.length * gFrameCaptureInterval;
  var realTimeElapsed = currentTime - gStartedAt;
  var diff = realTimeElapsed - idealTime;

  if (realTimeElapsed < gCaptureDuration) {
    // If duration isn't over shoot_ again.
    setTimeout(function() { shoot_(video, canvas, width, height); },
               gFrameCaptureInterval - diff);
  } else {
    // Done capturing!
    gCapturingStatus = 'done-capturing';
    prepareProgressBar_();
  }
}

/**
 * @private
 */
function captureFrame_(video, context, width, height) {
  context.drawImage(video, 0, 0, width, height);
  return context.getImageData(0, 0, width, height);
}

/**
 * @private
 */
function prepareProgressBar_() {
  document.body.innerHTML =
    '<html><body>' +
    '<p id="progressBar" style="position: absolute; top: 50%; left: 40%;">' +
    'Preparing to send frames.</p>' +
    '</body></html>';
}

/**
 * @private
 */
function updateProgressBar_(currentFrame) {
  progressBar.innerHTML =
    'Transferring captured frames: ' + '(' + currentFrame + '/' +
    gFrames.length + ')';
}
