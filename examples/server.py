import zmq
import json
from datetime import datetime
import os

def start_simple_server():
    context = zmq.Context()
    socket = context.socket(zmq.REP)
    socket.bind("tcp://0.0.0.0:8080")
    
    print("сервер запущен на порту 8080")
    print("Ожидание подключений от Android...")
    
    packet_count = 0
    data_file = "received_data.json"

    if os.path.exists(data_file):
        try:
            with open(data_file, 'r') as f:
                data = json.load(f)
                packet_count = data.get("count", 0)
        except:
            pass
    
    try:
        while True:
            message = socket.recv_string()
            packet_count += 1
            
            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            print(f"[{timestamp}] Получено #{packet_count}: {message}")
            
            packet_data = {
                "id": packet_count,
                "time": timestamp,
                "message": message
            }
            
            all_data = []
            if os.path.exists(data_file):
                try:
                    with open(data_file, 'r') as f:
                        all_data = json.load(f)
                except:
                    all_data = []
            
            all_data.append(packet_data)
            
            with open(data_file, 'w') as f:
                json.dump(all_data, f, indent=2)

            response = f"Hello from Server! Пакетов #{packet_count} получено"
            socket.send_string(response)
            print(f"[{timestamp}] Отправлено: {response}")
            
    except KeyboardInterrupt:
        print("\nСервер остановлен")
    finally:
        socket.close()
        context.term()

if __name__ == "__main__":
    start_simple_server()
