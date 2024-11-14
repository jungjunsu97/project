import cv2
import numpy as np
from os import listdir, makedirs
from os.path import isfile, join, isdir

# 얼굴 인식기 학습을 위한 함수
def train(name):
    data_path = "face/" + name + '/'
    face_pics = [f for f in listdir(data_path) if isfile(join(data_path, f))]

    Training_Data, Labels = [], []

    for i, file in enumerate(face_pics):
        image_path = data_path + face_pics[i]
        images = cv2.imread(image_path, cv2.IMREAD_GRAYSCALE)

        if images is None:
            continue

        Training_Data.append(np.asarray(images, dtype=np.uint8))
        Labels.append(i)

    if len(Labels) == 0:
        print(f"{name}에 대한 데이터가 충분하지 않습니다.")
        return None

    Labels = np.asarray(Labels, dtype=np.int32)

    # LBPH 얼굴 인식기 생성 및 학습
    model = cv2.face.LBPHFaceRecognizer_create()
    model.train(np.asarray(Training_Data), np.asarray(Labels))
    print(f"{name} 님의 모델 학습이 완료되었습니다!")

    model_dir = "model/"
    if not isdir(model_dir):
        makedirs(model_dir)

    model.save(model_dir + name + '_model.xml')

    return model

# 모든 사용자에 대해 모델을 학습하는 함수
def train_all_users():
    face_dir = "face/"
    users = [f for f in listdir(face_dir) if isdir(join(face_dir, f))]

    for user in users:
        print(f'{user} 사용자의 모델을 학습 중입니다...')
        train(user)

if __name__ == "__main__":
    train_all_users()
