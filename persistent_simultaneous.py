import requests
import threading
import http.client
import time

def send_request(conn, url, headers=None):
    conn.request("GET", url, headers=headers)
    response = conn.getresponse()
    response.read() 
    print(f"Request sent to {url}. Response: {response.status}, {response.reason}, {response.headers}")

num_requests = 10
conn = http.client.HTTPConnection("127.0.0.1", 8888)

threads = []
for _ in range(num_requests):
    t = threading.Thread(target=send_request(conn, '/css/style.css', headers={"Connection": "keep-alive"}))
    threads.append(t)
    t.start()
    time.sleep(5)

for t in threads:
    t.join()

conn.close()