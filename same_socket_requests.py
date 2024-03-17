import http.client
import time

def send_request(conn, method, url, headers=None):
    conn.request(method, url, headers=headers)
    response = conn.getresponse()
    response.read() 
    print(f"Request sent to {url}. Response: {response.status}, {response.reason}, {response.headers}")

conn = http.client.HTTPConnection("127.0.0.1", 8888)

urls = [
    ("/css/style.cs", 5), 
    ("/css/style.css", 5),
    ("/css/style.cs", 15)
]

for url, time_gap in urls:
    send_request(conn, "GET", url, headers={"Connection": "close"})
    time.sleep(time_gap)

conn.close()
