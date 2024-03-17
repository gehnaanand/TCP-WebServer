import requests
import threading

def send_request():
    url = "http://127.0.0.1:8888/css/style.css"  
    response = requests.get(url)
    print(response.status_code, response.headers)

num_requests = 10

threads = []
for _ in range(num_requests):
    t = threading.Thread(target=send_request)
    threads.append(t)
    t.start()

for t in threads:
    t.join()
