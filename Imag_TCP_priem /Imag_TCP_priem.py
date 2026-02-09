import cv2
import threading
import time
from TCP import TCPVideoReceiver  

port_resiver = 5015

def processing_loop(receiver: TCPVideoReceiver):
    while True:
        frame = receiver.get_frame()
        if frame is None:
            time.sleep(0.005)
            continue

        cv2.imshow("MFI", frame)

        if cv2.waitKey(1) & 0xFF == 27:
            break

receiver = TCPVideoReceiver(port=port_resiver)
receiver.start()

processing_thread = threading.Thread(
    target=processing_loop,
    args=(receiver,),
    daemon=True
)

processing_thread.start()

print("[MAIN] Press Ctrl+C to exit")

try:
    while True:
        time.sleep(1)
except KeyboardInterrupt:
    print("[MAIN] Stopping...")
    receiver.stop()
    cv2.destroyAllWindows()
