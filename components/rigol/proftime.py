import socket
import time

def measure_tcp_latency(server_ip, server_port, message):
    try:
        # Create a TCP socket
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            # Connect to the server
            sock.connect((server_ip, server_port))
            print(f"Connected to {server_ip}:{server_port}")

            # Record the time before sending the message
            start_time = time.time()

            # Send the message
            sock.sendall(message.encode())

            # Wait for the response
            response = sock.recv(1024)

            # Record the time after receiving the response
            end_time = time.time()

            # Calculate the round-trip time
            latency = (end_time - start_time) * 1000  # Convert to milliseconds

            print(f"Message sent: {message}")
            print(f"Response received: {response.decode()}")
            print(f"Round-trip time: {latency:.2f} ms")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    # Replace with your server's IP and port
    SERVER_IP = "127.0.0.1"
    SERVER_PORT = 5555
    MESSAGE = "*IDN?"

    measure_tcp_latency(SERVER_IP, SERVER_PORT, MESSAGE)

