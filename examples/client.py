import socket

def start_client():
    host = '127.0.0.1'
    port = 8080
    
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect((host, port))
    
    message = "Hello World!"
    client_socket.send(message.encode())
    
    response = client_socket.recv(1024).decode()
    print(f"*** received from server: {response}")
    
    client_socket.close()

if __name__ == "__main__":
    start_client()