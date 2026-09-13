import json
import asyncio
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
import paho.mqtt.client as mqtt

app = FastAPI()

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

class ConnectionManager:
    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        self.active_connections.remove(websocket)

    async def broadcast(self, message: str):
        for connection in self.active_connections:
            await connection.send_text(message)

manager = ConnectionManager()
loop = None

MQTT_BROKER = "broker.hivemq.com"
MQTT_TOPIC_TELEMETRY = "gridmitra/session/telemetry"
MQTT_TOPIC_START = "gridmitra/session/start"

def on_message(client, userdata, msg):
    payload = msg.payload.decode()
    print(f"MQTT Data Received: {payload}")
    if loop and loop.is_running():
        asyncio.run_coroutine_threadsafe(manager.broadcast(payload), loop)

mqtt_client = mqtt.Client()
mqtt_client.on_message = on_message

@app.on_event("startup")
def startup_event():
    global loop
    loop = asyncio.get_event_loop()
    mqtt_client.connect(MQTT_BROKER, 1883, 60)
    mqtt_client.subscribe(MQTT_TOPIC_TELEMETRY)
    mqtt_client.subscribe(MQTT_TOPIC_START)
    mqtt_client.loop_start()

@app.websocket("/ws/session")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(websocket)

@app.post("/api/hardware/button")
async def hardware_button(payload: dict):
    message = json.dumps({"type": "button", **payload})
    await manager.broadcast(message)
    return {"status": "ok"}

@app.post("/api/hardware/reading")
async def hardware_reading(payload: dict):
    message = json.dumps({"type": "reading", **payload})
    await manager.broadcast(message)
    return {"status": "ok"}