'''
This python script is designed to connect to the esp32 via a BLE server then read and write commands from and to a GATT characteristic
'''

import asyncio
from bleak import BleakScanner
from bleak import BleakClient
import array

async def main():
    print("Beginning scan for devices.")
    devices = await BleakScanner.discover()
    ble_address = "E4:65:B8:82:60:8E"
    ble_name = "XYFLYBLE"
    characteristic_uuid = "c57884e4-fae7-4370-81cf-6b4919b28042"
    for device in devices:
        print(device)
        if device.name == ble_name:
            async with BleakClient(ble_address) as client:
                print("Connected to XYFLYBLE")
                while client.is_connected:
                    data = await client.read_gatt_char(characteristic_uuid)
                    new_data = "reset"
                    new_data = new_data.encode('ascii')
                    print("Old data: ")
                    print(data)
                    print("New data: ")
                    print(new_data)
                    if(new_data == data):
                        print("Command already written to characteristic resetting...")
                        break
                    print("Writing new data to characterisitic...")
                    await client.write_gatt_char(characteristic_uuid, new_data)

asyncio.run(main())
