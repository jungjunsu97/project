import cv2
import numpy as np
from os import listdir, makedirs
from os.path import isfile, join, exists
from datetime import datetime  
import socket
import time
import threading

# 전역 플래그로 카메라 상태를 관리
camera_active = True

def load_dnn_model():
    model_file = "res10_300x300_ssd_iter_140000_fp16.caffemodel"
    config_file = "deploy.prototxt.txt"
    net = cv2.dnn.readNetFromCaffe(config_file, model_file)
    return net


def face_extractor_dnn(img, net):
    blob = cv2.dnn.blobFromImage(img, 1.0, (300, 300), (104.0, 177.0, 123.0))
    net.setInput(blob)
    detections = net.forward()

    h, w = img.shape[:2]
    faces = []
    face_box = None  

    for i in range(detections.shape[2]):
        confidence = detections[0, 0, i, 2]

        if confidence > 0.7:
            box = detections[0, 0, i, 3:7] * np.array([w, h, w, h])
            (x, y, x1, y1) = box.astype("int")
            face_box = (x, y, x1, y1)  
            cropped_face = img[y:y1, x:x1]
            faces.append(cropped_face)

    if len(faces) > 0:
        return faces[0], face_box  
    else:
        return None, None  


def load_models():
    model_dir = "model/"
    models = {}

    model_files = [f for f in listdir(model_dir) if isfile(join(model_dir, f))]
    for file in model_files:
        name = file.split('_model.xml')[0]
        model = cv2.face.LBPHFaceRecognizer_create()
        model.read(join(model_dir, file))
        models[name] = model
    return models


def get_current_time_str():
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def receive_socket_data(s, stranger_dir, cap):
    global camera_active, frame  # 전역 플래그 및 frame 사용
    while True:
        try:
            data = s.recv(1024).decode()
            if data == 'FR:room_201:request_capture':
                print("캡처 요청을 받았습니다.")
                
                # 카메라 스트림 중지
                camera_active = False
                time.sleep(1)  # 안전하게 카메라가 중지되도록 대기
                
                current_time = get_current_time_str()
                capture = f'capture_{current_time}.jpg'
                capture_path = join(stranger_dir, capture)

                if frame is not None:
                    cv2.imwrite(capture_path, frame)
                    print(f"캡처된 이미지가 저장되었습니다: {capture_path}")
                    s.sendall(f'FR:room_201:capture:{capture}'.encode())
                else:
                    print("오류: 유효한 프레임이 없습니다.")

                # 카메라 스트림 재개
                camera_active = True
        except socket.error as e:
            print(f"소켓 오류: {e}")
            break


def run(models, stranger_dir):
    global camera_active, frame

    net = load_dnn_model() 
    cap = cv2.VideoCapture(0)  

    if not cap.isOpened():
        print("웹캠을 열 수 없습니다.")
        return

    confidence_suc = 0
    confidence_fai = 0
    confidence_cnt = 0

    window_name = 'Face Recognition'
    cv2.namedWindow(window_name, cv2.WND_PROP_FULLSCREEN)
    cv2.setWindowProperty(window_name, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_NORMAL)
    cv2.resizeWindow(window_name, 1024, 600)

    HOST = '192.168.0.15' 
    PORT = 9000  

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((HOST, PORT))
        s.sendall(b'FR:room_201')  

        # 소켓 스레드 시작
        socket_thread = threading.Thread(target=receive_socket_data, args=(s, stranger_dir, cap))
        socket_thread.daemon = True 
        socket_thread.start()

        while True:
            if camera_active:
                ret, frame = cap.read()
                if not ret:
                    print("웹캠에서 프레임을 읽을 수 없습니다.")
                    break

                original_frame = cv2.resize(frame, (512, 600))  
                face, face_box = face_extractor_dnn(frame, net) 

                try:
                    min_score = 999
                    min_score_name = ""
                    confidence = 0

                    if face is not None:
                        face_gray = cv2.cvtColor(face, cv2.COLOR_BGR2GRAY)
                        face_resized = cv2.resize(face_gray, (200, 200))

                        for name, model in models.items():
                            result = model.predict(face_resized)
                            if min_score > result[1]:
                                min_score = result[1]
                                min_score_name = name

                        if min_score < 500:
                            confidence = int(100 * (1 - (min_score) / 300))
                            display_string = str(confidence) + '% ' + min_score_name
                        else:
                            display_string = "잠금 상태"

                        if face_box is not None:
                            x, y, x1, y1 = face_box
                            cv2.rectangle(frame, (x, y), (x1, y1), (0, 255, 0), 2)

                        cv2.putText(frame, display_string, (50, 50), cv2.FONT_HERSHEY_COMPLEX, 2, (0, 255, 0), 2)
                        confidence_cnt += 1

                        if confidence_cnt < 21:
                            if confidence >= 85:
                                cv2.putText(frame, "Unlocked - " + min_score_name, (50, 450), cv2.FONT_HERSHEY_COMPLEX, 3, (0, 255, 0), 2)
                                confidence_suc += 1
                            else:
                                cv2.putText(frame, "Locked", (50, 450), cv2.FONT_HERSHEY_COMPLEX, 3, (0, 0, 255), 2)
                                confidence_fai += 1
                            
                            if confidence_cnt == 20:
                                if confidence_suc >= 15:
                                    s.sendall(b'FR:room_201:success:')
                                    print('a')
                                    time.sleep(30)
                                elif confidence_fai > 5:
                                    current_time = get_current_time_str()
                                    failed_img = f'{current_time}.jpg'
                                    failed_img_path = join(stranger_dir, failed_img)
                                    cv2.imwrite(failed_img_path, frame)
                                    #print(f"Failed capture saved at: {failed_img}")
                                    s.sendall(f'FR:room_201:failure:{failed_img}'.encode())
                                    print('b')
                                    time.sleep(10)
                            
                        else:
                            confidence_suc = 0
                            confidence_fai = 0
                            confidence_cnt = 0

                    if frame is not None:
                        image_resized = cv2.resize(frame, (512, 600))  
                    else:
                        image_resized = np.zeros((600, 512, 3), dtype=np.uint8)  

                    combined_frame = np.hstack((original_frame, image_resized))  
                    cv2.imshow(window_name, combined_frame)  

                except Exception as e:
                    print(f"Error: {str(e)}")

            if cv2.waitKey(1) == 13: 
                break

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    stranger_dir = "/home/choi/Desktop/smartdoorlock/images/"
    if not exists(stranger_dir):
        makedirs(stranger_dir)
    models = load_models()
    if len(models) == 0:
        print("학습된 모델이 없습니다. 먼저 모델을 학습시켜주세요.")
    else:
        run(models, stranger_dir)
