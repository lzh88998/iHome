import hmac
import hashlib
import datetime

import json

import time

import redis

import random
import string

from threading import Thread, Lock

from paho.mqtt import client as mc
import paho.mqtt.client as mqtt

def generate_hmac_sha256(secret_key, message):
  hmac_object=hmac.new(secret_key.encode(), message.encode(), hashlib.sha256)
  return hmac_object.hexdigest()

client_connected=False
def on_connect(client, userdata, flags, rc, properties):
  global client_connected
  print("on_connect")
  if rc.is_failure:
    print(f"Failed to connect, return code {rc} {userdata} {flags} {properties}")
  else:
    print("Connected to MQTT Broker!")
    client_connected=True

device_id="268735387aba2abc25v4op"
def connect_mqtt():
  broker="m1.tuyacn.com"
  port=8883

  secret="ocSWff4yJ2BnTtYO"

  now=int(datetime.datetime.now().timestamp())
  client_id=f"tuyalink_{device_id}"
  username=f"{device_id}|signMethod=hmacSha256,timestamp={now},secureMode=1,accessType=1"
  pwd_content=f"deviceId={device_id},timestamp={now},secureMode=1,accessType=1"
  password=generate_hmac_sha256(secret, pwd_content)
  client=mc.Client(mqtt.CallbackAPIVersion.VERSION2, client_id)
  client.tls_set();
  client.username_pw_set(username, password)
  client.on_connect=on_connect
  client.connect(broker, port)
  return client

lock=Lock()
thread_data={}
b_exit=False
def thread_work(interval, lock):
  global thread_data
  global client
  global b_exit
  while not b_exit:
    topic=f"tylink/{device_id}/thing/property/report"
    pub_now=int(datetime.datetime.now().timestamp())
    time.sleep(interval)
    with lock:
      if "data" in thread_data:
        thread_data["msgId"]=''.join(random.choices(string.ascii_letters+string.digits,k=32))
        thread_data["time"]=f"{pub_now}"
        client.publish(topic, json.dumps(thread_data), qos=1)
        thread_data={}

def pub_msg(client, property_id, dp_value):
  global thread_data
  pub_now=int(datetime.datetime.now().timestamp())
  dp_id=property_id+1
  with lock:
    if "data" not in thread_data:
      thread_data["data"]={}
    thread_data["data"][dp_id]={}
    thread_data["data"][dp_id]["value"]=dp_value
    thread_data["data"][dp_id]["time"]=f"{pub_now}"

enabled_code={1, 2, 3, 4, 5, 6, 7, 8, 9, 38, 39, 40, 41, 42, 43, 44, 46, 71, 72, 73, 74, 76, 78, 80}
redis_code_field={}
redis_field_code={}
def on_msg(client, userdata, msg):
  global client_connected
  global redis_code_field
  global redis_client
  if msg.topic == f"tylink/{device_id}/thing/property/set":
    #print(client)
    #print(userdata)
    #print(msg.topic)
    #print(msg.payload)
    payload=json.loads(msg.payload.decode("utf-8"))
    #print(payload["data"].keys())
    for key in list(payload["data"].keys()):
      property_id=int(key)-1
      redis_topic=redis_code_field[property_id]
      print(f"{redis_topic}:{payload['data'][key]}")
      redis_client.publish(redis_topic, 1 if payload["data"][key] else 0, qos=1)
  elif msg.topic == f"tylink/{device_id}/thing/model/get_response":
    payload=json.loads(msg.payload.decode("utf-8"))
    if payload["code"] == 0:
      for property in payload["data"]["services"][0]["properties"]:
        property_code=int(property["code"])-1
        if not property_code in enabled_code:
          print(f"Error {property_code} not in enabled list!")
          continue

        if property_code < 32:
          key_str="cargador/192.168.1.210"
        elif property_code < 64:
          key_str="cargador/192.168.1.211"
        elif property_code < 96:
          key_str="cargador/192.168.1.212"
        else:
          print("Error invalid code!")
          continue
        query_code=property_code%32
        redis_value=redis_client.hget(key_str, query_code)
        if redis_value is None:
          print(f"Error invalid item {key_str}.{query_code}")
          continue
        if redis_value in redis_field_code:
          print(f"Warning field {redis_value} already exists!")
          continue
        redis_code_field[property_code]=redis_value
        redis_field_code[redis_value]=property_code
      client_connected=True
    else:
      print(f"Error unknown topic {msg_topic}!")

def on_pub(client, userdata, flags, rc, properties):
  print("On Pub")
  if rc.is_failure:
    print("Failed to publish!")
    print(client)
    print(userdata)
    print(flags)
    print(rc)
    print(properties)

def on_sub(client, userdata, flags, rc, properties):
  global client_connected
  if rc[0].is_failure:
    print("Failed to subscribe!")
  else:
    print("Subscribe successful!")
    client_connected=True

client_connected=False

client=connect_mqtt()

client.on_message=on_msg
#client.on_publish=on_pub
client.on_subscribe=on_sub

client.loop_start()

redis_client=redis.Redis(host='localhost', port=6379)
redis_client.ping()
pubsub=redis_client.pubsub()

thread_handle=Thread(target=thread_work, args=(1, lock))
thread_handle.start()

#for o in dir(redis_client):
#  print(o)
#  time.sleep(1)

print('Waiting for connect...')
while client_connected==False:
  print(client_connected)
  time.sleep(1)

print('Subscribing device model from cloud...')
client_connected=False
client.subscribe(f"tylink/{device_id}/thing/model/get_response", qos=1)

print('Waiting for finish subscribing...')
while client_connected==False:
  print(client_connected)
  time.sleep(1)

#print('Subscribing 2...')
#client_connected=False
#client.subscribe(f"tylink/{device_id}/thing/property/report_response", qos=1)
#
#print('Waiting for subscribing 2...')
#while client_connected==False:
#  print(client_connected)
#  time.sleep(1)

print('Subscribing set device property message from cloud...')
client_connected=False
client.subscribe(f"tylink/{device_id}/thing/property/set", qos=1)

print('Waiting for finish subscribing...')
while client_connected==False:
  print(client_connected)
  time.sleep(1)

print('Trying to get device model from cloud...')
client_connected=False
now=int(datetime.datetime.now().timestamp())
req={}
req["msgId"]=f"{device_id}{now}"
req["time"]=now
result=client.publish(f"tylink/{device_id}/thing/model/get", json.dumps(req))
print(f'Publish request {result[0]}.{result[1]}')
print('Waiting for device model...')
while client_connected==False:
  print(client_connected)
  time.sleep(1)

print('Subscribing local changes according to device model')

for code in redis_code_field.keys():
  redis_client.select(1)
  temp_result=redis_client.get(redis_code_field[code])
  comp_result=False if temp_result is None else int(temp_result)==1
  print(f'{redis_code_field[code]}:{temp_result}:{comp_result}')
  redis_client.select(0)
  print(f"{code}:{comp_result}")
  pub_msg(client, code, comp_result)
  #time.sleep(1)
  pubsub.subscribe(redis_code_field[code])

for msg in pubsub.listen():
  print(msg)
  if msg["type"] != "message":
    continue
  msg_code=redis_field_code[msg["channel"]]
  stat=False if msg["data"] is None else int(msg["data"])==1
  print(f'test:{msg_code}:{stat}')
  pub_msg(client, msg_code, stat)

thread_handle.join()
client.loop_stop()

