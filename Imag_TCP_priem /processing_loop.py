import cv2
import time
from TCP import TCPVideoReceiver
import threading
from processed_frame import ProcessedFrameBuffer
from yolotracking import YoloTrackImage  # YOLO

class ProcessedFrameBuffer:
    def __init__(self):
        self.frame = None
        self.lock = threading.Lock()

    def set(self, frame):
        with self.lock:
            self.frame = frame.copy()

    def get(self):
        with self.lock:
            return None if self.frame is None else self.frame.copy()


def processing_loop(receiver: TCPVideoReceiver, regim: int, buffer: ProcessedFrameBuffer):
    while True:
        frame = receiver.get_frame()
        if frame is None:
            time.sleep(0.005)
            continue

        if regim == 1:
            processed = frame

        elif regim == 2:
            processed = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

        elif regim == 3:
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            processed = cv2.Canny(gray, 80, 160)

        elif regim == 4:
            # YOLO Tracking
            # processed = YoloTrackImage(frame)
            processed = frame

        else:
            processed = frame

        # Гарантируем 3 канала (важно для TCP)
        if len(processed.shape) == 2:
            processed = cv2.cvtColor(processed, cv2.COLOR_GRAY2BGR)

        buffer.set(processed)

        cv2.imshow("Processed", processed)

        if cv2.waitKey(1) & 0xFF == 27:
            break
