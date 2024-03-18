# import requests
# import threading

# def send_request():
#     url = "http://127.0.0.1:8888/css/style.css"  
#     response = requests.get(url)
#     print(response.status_code, response.headers)

# num_requests = 10

# threads = []
# for _ in range(num_requests):
#     t = threading.Thread(target=send_request)
#     threads.append(t)
#     t.start()

# for t in threads:
#     t.join()

import requests
import concurrent.futures

def make_request(url):
    try:
        response = requests.get(url)
        print(f"Response from {url}: {response.status_code}")
    except requests.exceptions.RequestException as e:
        print(f"Error while fetching {url}: {e}")

urls = [
    'http://127.0.0.1:8888/css/style.css',
    'http://127.0.0.1:8888/css/style.css',
    'http://127.0.0.1:8888/css/style.css'
]

MAX_CONCURRENT_REQUESTS = 5

with concurrent.futures.ThreadPoolExecutor(max_workers=MAX_CONCURRENT_REQUESTS) as executor:
    futures = [executor.submit(make_request, url) for url in urls]
    concurrent.futures.wait(futures)
