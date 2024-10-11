import argparse
import asyncio
import datetime
import json
import netrc
import ssl
import subprocess
from collections import defaultdict

import aiomqtt
import toml


def create_client(args):
    if not args.no_tls:
        tls_params = aiomqtt.TLSParameters(
            cert_reqs=ssl.CERT_REQUIRED,
            tls_version=ssl.PROTOCOL_TLSv1_2,
        )
    else:
        tls_params = None

    if args.username is None and args.password is None:
        rc = netrc.netrc()
        username, _, password = rc.authenticators(args.broker)
    else:
        username, password = (args.username, args.password)

    return aiomqtt.Client(
        hostname=args.broker,
        username=username,
        password=password,
        port=args.port if args.port is not None else 1883 if args.no_tls else 8883,
        tls_params=tls_params,
    )


async def send_config(args):
    with open(args.config) as f:
        config = toml.load(f)

    peers = list(config["devices"].keys())
    defaults = config.get("defaults", {})

    async with create_client(args) as client:
        if args.mac:
            devices = args.mac
        else:
            devices = config["devices"].keys()

        for mac in devices:
            await client.publish(
                f"calan-mai/lights/{mac}/config/set",
                json.dumps(defaults | config["devices"][mac]),
            )
            await client.publish(f"calan-mai/lights/{mac}/peers/set", json.dumps(peers))


async def send_firmware(args):
    if not args.no_build:
        subprocess.run(["idf.py", "build"], check=True)

        with open("version.txt", "r+") as f:
            v = int(f.read())
            f.seek(0)
            f.write(str(v + 1))
            f.truncate()

        with open("date.txt", "w") as f:
            t = (
                datetime.datetime.now()
                .astimezone(tz=datetime.timezone.utc)
                .replace(microsecond=0)
            )
            f.write(t.isoformat())

        subprocess.run(["idf.py", "build"], check=True)

    with open(args.config) as f:
        config = toml.load(f)
    with open(args.firmware, "rb") as f:
        firmware = f.read()

    if args.mac:
        devices = args.mac
    else:
        devices = config["devices"].keys()

    async with create_client(args) as client:
        for mac in devices:
            await client.publish(f"calan-mai/lights/{mac}/ota/firmware", firmware)


async def send_command(args):
    with open(args.config) as f:
        config = toml.load(f)

    if args.mac:
        devices = args.mac
    else:
        devices = config["devices"].keys()

    async with create_client(args) as client:
        for mac in devices:
            await client.publish(f"calan-mai/lights/{mac}/command", args.value)


async def cleanup(args):
    async with create_client(args) as client:
        offline = set()
        topics = defaultdict(list)
        async with client.messages() as messages:
            await client.subscribe("calan-mai/lights/+/#")
            try:
                async with asyncio.timeout(0.5):
                    async for message in messages:
                        mac = str(message.topic).split("/")[2]
                        trailing = str(message.topic).split("/")[3:]
                        topics[mac].append(str(message.topic))
                        if trailing == ["status"] and message.payload == b"Offline":
                            mac = str(message.topic).split("/")[2]
                            offline.add(mac)

            except TimeoutError:
                pass

            for mac in offline:
                for t in topics[mac]:
                    await client.publish(t, None, retain=True)

        pass


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--broker", default="mqtt.lietar.net")
    parser.add_argument("--port", type=int)
    parser.add_argument("--username", "-u")
    parser.add_argument("--password", "-P")
    parser.add_argument("--no-tls", action="store_true")

    subparsers = parser.add_subparsers(required=True)

    p = subparsers.add_parser("ota")
    p.set_defaults(func=send_firmware)
    p.add_argument("--no-build", action="store_true")
    p.add_argument("--config", default="config.toml")
    p.add_argument("--firmware", default="build/light-control.bin")
    p.add_argument("mac", nargs="*")

    p = subparsers.add_parser("config")
    p.set_defaults(func=send_config)
    p.add_argument("--config", default="config.toml")
    p.add_argument("mac", nargs="*")

    p = subparsers.add_parser("restart")
    p.set_defaults(func=send_command, value="restart")
    p.add_argument("--config", default="config.toml")
    p.add_argument("mac", nargs="*")

    p = subparsers.add_parser("on")
    p.set_defaults(func=send_command, value="ON")
    p.add_argument("--config", default="config.toml")
    p.add_argument("mac", nargs="*")

    p = subparsers.add_parser("off")
    p.set_defaults(func=send_command, value="OFF")
    p.add_argument("--config", default="config.toml")
    p.add_argument("mac", nargs="*")

    p = subparsers.add_parser("cleanup")
    p.set_defaults(func=cleanup)
    args = parser.parse_args()
    await args.func(args)


if __name__ == "__main__":
    asyncio.run(main())
