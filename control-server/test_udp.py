import socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", 7070))
print("Listening on 7070...")
while True:
    try:
        data, addr = sock.recvfrom(2048)
        if len(data) >= 5:
            print(f"Packet from {addr}, CamID: {data[0]}")
    except Exception as e:
        print("err", e)
