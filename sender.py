import requests
import time

ESP_IP = "192.168.18.220"  # Ganti kalau IP ESP32 berubah

while True:
    text = input("Masukkan teks: ")

    requests.get(
        f"http://{ESP_IP}/text",
        params={"msg": text},
        timeout=3
    )