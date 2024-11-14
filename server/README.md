# 통합서버(Linux)

- TCP/IP 기능을 활용한 클라이언트 소켓 관리(Web, AI, ESP)
- 자료구조(Linked list)를 이용하여 호수별 ESP, FR소켓관리  
  ex) 구조체 RoomNode{RoomNO, ESP32, FR, RoomNode* next} (Web 소켓은 공통 소켓으로 사용)
- 멀티스레드를 활용하여 소켓별로 스레드 분리 및 메시지 확인 및 처리(중요 데이터는 뮤텍스)
- 데이터베이스 SELECT 및 UPDATE, INSERT 기능
- Web 메시지 처리 : 원격 문 제어(open 메시지를 ESP소켓으로 전달) / 로그인 패스워드 변경(DB UPDATE)
- FR 메시지 처리 : 얼굴인식 성공 시 ESP 키패드 활성화(active_keypad 메세지를 ESP소켓으로 전달) / 5회 이상 얼굴인식 실패 시 캡쳐 이미지 저장(DB INSERT INTO Img_Path)
- ESP 메시지 처리 : 5회 이상 RFID, 패스워드 실패 시 FR 소켓으로 이미지 캡쳐 명령 전송 후 받은 이미지를 저장(DB INSERT)
