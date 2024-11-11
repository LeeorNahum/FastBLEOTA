import os
import asyncio
from bleak import BleakClient, BleakScanner
import struct
import sys
import time
import argparse
from collections import deque
from bleak.exc import BleakError

SERVICE_UUID = "4e8cbb5e-bc0f-4aab-a6e8-55e662418bef"
CHARACTERISTIC_UUID = "513fcda9-f46d-4e41-ac4f-42b768495a85"

time_deque = deque(maxlen=10)

def calculate_time_remaining(elapsed_times_deque, bytes_remaining, chunk_size):
    if len(elapsed_times_deque) == 0:
        return "Calculating..."

    average_time = sum(elapsed_times_deque) / len(elapsed_times_deque)
    estimated_time_remaining = (bytes_remaining / chunk_size) * average_time

    minutes, seconds = divmod(estimated_time_remaining, 60)
    return f"{int(minutes)} minutes and {seconds:.2f} seconds remaining"

async def send_firmware(address, file_path):
    device = await BleakScanner.find_device_by_address(address, timeout=10.0)
    disconnected_event = asyncio.Event()

    if not device:
        print(f"Device with address {address} could not be found.")
        return

    def handle_disconnect(_: BleakClient):
        print("Device disconnected")
        disconnected_event.set()

    async def send_data(client: BleakClient, data: bytearray, response: bool):
        start_time = time.time()
        await client.write_gatt_char(CHARACTERISTIC_UUID, data, response)
        end_time = time.time()
        elapsed_time = end_time - start_time
        return elapsed_time

    try:
        async with BleakClient(device, disconnected_callback=handle_disconnect) as client:
            mtu_size = client.mtu_size
            print(f"Negotiated MTU size: {mtu_size}")
            # Since MTU exchange isn't available on Windows, we use a fixed chunk size
            chunk_size = mtu_size - 3  # Adjust as needed
            print(f"Using chunk size: {chunk_size} bytes")

            file_size = os.path.getsize(file_path)
            await client.write_gatt_char(CHARACTERISTIC_UUID, struct.pack("<I", file_size), response=True)
            print(f"Sent file size: {file_size} bytes")

            total_packets = (file_size + chunk_size - 1) // chunk_size
            packet_number = 0

            total_sent = 0
            total_start_time = time.time()
            initial_estimated_time_printed = False

            with open(file_path, 'rb') as f:
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    packet_number += 1

                    # Measure the time taken to send the packet
                    elapsed_time = await send_data(client, chunk, response=False)
                    time_deque.append(elapsed_time)
                    total_sent += len(chunk)

                    # After the first few packets, calculate initial estimated total time
                    if packet_number == 5 and not initial_estimated_time_printed:
                        average_time = sum(time_deque) / len(time_deque)
                        estimated_time_total = average_time * total_packets
                        est_minutes, est_seconds = divmod(estimated_time_total, 60)
                        print(f"Initial estimated time to complete: {int(est_minutes)} minutes and {est_seconds:.2f} seconds")
                        initial_estimated_time_printed = True

                    percentage = (total_sent / file_size) * 100
                    bytes_remaining = file_size - total_sent
                    time_remaining = calculate_time_remaining(time_deque, bytes_remaining, chunk_size)
                    print(f"Packet {packet_number}/{total_packets}: Sent {total_sent}/{file_size} bytes ({percentage:.2f}%) in {elapsed_time:.4f} seconds. {time_remaining}")
                    # No delay needed

            total_end_time = time.time()
            total_elapsed_time = total_end_time - total_start_time
            elapsed_minutes, elapsed_seconds = divmod(total_elapsed_time, 60)
            print(f"\nTotal time elapsed: {int(elapsed_minutes)} minutes and {elapsed_seconds:.2f} seconds")

            # Compare initial estimated time with actual elapsed time
            if initial_estimated_time_printed:
                time_difference = total_elapsed_time - estimated_time_total
                diff_minutes, diff_seconds = divmod(abs(time_difference), 60)
                if time_difference > 0:
                    print(f"The update took {int(diff_minutes)} minutes and {diff_seconds:.2f} seconds longer than estimated.")
                else:
                    print(f"The update was completed {int(diff_minutes)} minutes and {diff_seconds:.2f} seconds faster than estimated.")

            # Calculate average time per packet and throughput
            if packet_number > 0:
                average_time_per_packet = total_elapsed_time / packet_number
                throughput_bytes_per_second = total_sent / total_elapsed_time  # Bytes per second
                throughput_megabytes_per_second = throughput_bytes_per_second / (1024 * 1024)  # Convert to MB/s
                print(f"Average time per packet: {average_time_per_packet:.4f} seconds")
                print(f"Average throughput: {throughput_bytes_per_second:.2f} bytes/second ({throughput_megabytes_per_second:.2f} MB/s)")

            print("All data sent, waiting for the device to disconnect...")
            await disconnected_event.wait()

    except BleakError as e:
        print(f"Bleak error occurred: {e}")
    except OSError as e:
        print(f"An OS error occurred: {e}")
    except Exception as e:
        print(f"Unexpected error: {e}")

def main():
    parser = argparse.ArgumentParser(description="BLE OTA Firmware Uploader")
    parser.add_argument('--address', type=str, help='BLE device address', required=True)
    parser.add_argument('--file', type=str, help='Firmware file path', required=True)

    args = parser.parse_args()

    address = args.address
    firmware_path = args.file

    if not os.path.exists(firmware_path):
        print(f"File not found: {firmware_path}")
        sys.exit(1)

    asyncio.run(send_firmware(address, firmware_path))

if __name__ == "__main__":
    main()
