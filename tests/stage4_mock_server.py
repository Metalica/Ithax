"""Loopback mock server for the Stage 4 network integration gate.

A test harness, not a server-side implementation: it implements only the
PLACEBO handshake wire exchange needed to prove the client network path.
It contains no game logic, no world state, and no account store.

Usage:
    stage4_mock_server.py --port 26001
"""

import argparse
import json
import socket
import struct
import sys
import threading

LOOPBACK_ADDRESS = "127.0.0.1"
LISTEN_BACKLOG = 1
MAX_FRAME_BYTES = 1024 * 1024
SOCKET_TIMEOUT_SECONDS = 30.0

OP_NONE = 0x01
OP_TOKEN = 0x02
OP_LONG_LONG = 0x03
OP_LONG = 0x04
OP_SIGNED_SHORT = 0x05
OP_BYTE = 0x06
OP_MINUS_ONE = 0x07
OP_ZERO_INT = 0x08
OP_ONE_INT = 0x09
OP_REAL = 0x0A
OP_ZERO_REAL = 0x0B
OP_BUFFER = 0x0D
OP_EMPTY_STRING = 0x0E
OP_CHAR_STRING = 0x0F
OP_SHORT_STRING = 0x10
OP_STRING_TABLE = 0x11
OP_LONG_STRING = 0x13
OP_TUPLE = 0x14
OP_LIST = 0x15
OP_DICT = 0x16
OP_OBJECT = 0x17
OP_TRUE = 0x1F
OP_FALSE = 0x20
OP_EMPTY_TUPLE = 0x24
OP_ONE_TUPLE = 0x25
OP_EMPTY_LIST = 0x26
OP_ONE_LIST = 0x27
OP_EMPTY_WSTRING = 0x28
OP_TWO_TUPLE = 0x2C
OP_WSTRING_UTF8 = 0x2E
OP_VAR_INT = 0x2F
MARSHAL_HEADER = 0x7E
OPCODE_MASK = 0x3F

STRING_TABLE = [
    "*corpid", "*locationid", "age", "Asteroid", "authentication",
    "ballID", "beyonce", "bloodlineID", "capacity", "categoryID",
    "character", "characterID", "characterName", "characterType", "charID",
    "chatx", "clientID", "config", "contraband", "corporationDateTime",
    "corporationID", "createDateTime", "customInfo", "description",
    "divisionID", "DoDestinyUpdate", "dogmaIM", "EVE System", "flag",
    "foo.SlimItem", "gangID", "Gemini", "gender", "graphicID", "groupID",
    "header", "idName", "invbroker", "itemID", "items", "jumps", "line",
    "lines", "locationID", "locationName", "macho.CallReq", "macho.CallRsp",
    "macho.MachoAddress", "macho.Notification",
    "macho.SessionChangeNotification", "modules", "name", "objectCaching",
    "objectCaching.CachedObject", "OnChatJoin", "OnChatLeave", "OnChatSpeak",
    "OnGodmaShipEffect", "OnItemChange", "OnModuleAttributeChange",
    "OnMultiEvent", "orbitID", "ownerID", "ownerName", "quantity", "raceID",
    "RowClass", "securityStatus", "Sentry Gun", "sessionchange", "singleton",
    "skillEffect", "squadronID", "typeID", "used", "userID",
    "util.CachedObject", "util.IndexRowset", "util.Moniker", "util.Row",
    "util.Rowset", "*multicastID", "AddBalls", "AttackHit3", "AttackHit3R",
    "AttackHit4R", "DoDestinyUpdates", "GetLocationsEx",
    "InvalidateCachedObjects", "JoinChannel", "LSC", "LaunchMissile",
    "LeaveChannel", "OID+", "OID-", "OnAggressionChange", "OnCharGangChange",
    "OnCharNoLongerInStation", "OnCharNowInStation", "OnDamageMessage",
    "OnDamageStateChange", "OnEffectHit", "OnGangDamageStateChange", "OnLSC",
    "OnSpecialFX", "OnTarget", "RemoveBalls", "SendMessage", "SetMaxSpeed",
    "SetSpeedFraction", "TerminalExplosion", "address", "alert", "allianceID",
    "allianceid", "bid", "bookmark", "bounty", "channel", "charid",
    "constellationid", "corpID", "corpid", "corprole", "damage", "duration",
    "effects.Laser", "gangid", "gangrole", "hqID", "issued", "jit",
    "languageID", "locationid", "machoVersion", "marketProxy", "minVolume",
    "orderID", "price", "range", "regionID", "regionid", "role",
    "rolesAtAll", "rolesAtBase", "rolesAtHQ", "rolesAtOther", "shipid", "sn",
    "solarSystemID", "solarsystemid", "solarsystemid2", "source", "splash",
    "stationID", "stationid", "target", "userType", "userid", "volEntered",
    "volRemaining", "weapon",
    "agent.missionTemplatizedContent_BasicKillMission",
    "agent.missionTemplatizedContent_ResearchKillMission",
    "agent.missionTemplatizedContent_StorylineKillMission",
    "agent.missionTemplatizedContent_GenericStorylineKillMission",
    "agent.missionTemplatizedContent_BasicCourierMission",
    "agent.missionTemplatizedContent_ResearchCourierMission",
    "agent.missionTemplatizedContent_StorylineCourierMission",
    "agent.missionTemplatizedContent_GenericStorylineCourierMission",
    "agent.missionTemplatizedContent_BasicTradeMission",
    "agent.missionTemplatizedContent_ResearchTradeMission",
    "agent.missionTemplatizedContent_StorylineTradeMission",
    "agent.missionTemplatizedContent_GenericStorylineTradeMission",
    "agent.offerTemplatizedContent_BasicExchangeOffer",
    "agent.offerTemplatizedContent_BasicExchangeOffer_ContrabandDemand",
    "agent.offerTemplatizedContent_BasicExchangeOffer_Crafting",
    "agent.LoyaltyPoints", "agent.ResearchPoints", "agent.Credits",
    "agent.Item", "agent.Entity", "agent.Objective", "agent.FetchObjective",
    "agent.EncounterObjective", "agent.DungeonObjective",
    "agent.TransportObjective", "agent.Reward", "agent.TimeBonusReward",
    "agent.MissionReferral", "agent.Location", "agent.StandardMissionDetails",
    "agent.OfferDetails", "agent.ResearchMissionDetails",
    "agent.StorylineMissionDetails",
]

STRING_TO_INDEX = {}
for _index, _text in enumerate(STRING_TABLE, start=1):
    STRING_TO_INDEX.setdefault(_text, _index)


class MockError(Exception):
    pass


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def read(self, count):
        if self.pos + count > len(self.data):
            raise MockError("truncated marshal stream")
        chunk = self.data[self.pos:self.pos + count]
        self.pos += count
        return chunk

    def u8(self):
        return self.read(1)[0]

    def u32(self):
        return struct.unpack("<I", self.read(4))[0]

    def i64(self):
        return struct.unpack("<q", self.read(8))[0]

    def double(self):
        return struct.unpack("<d", self.read(8))[0]

    def size_ex(self):
        first = self.u8()
        if first == 0xFF:
            return self.u32()
        return first


def decode_value(reader):
    raw = reader.u8()
    opcode = raw & OPCODE_MASK
    if opcode == OP_NONE:
        return None
    if opcode == OP_TRUE:
        return True
    if opcode == OP_FALSE:
        return False
    if opcode == OP_ZERO_INT:
        return 0
    if opcode == OP_ONE_INT:
        return 1
    if opcode == OP_MINUS_ONE:
        return -1
    if opcode == OP_BYTE:
        return struct.unpack("<b", reader.read(1))[0]
    if opcode == OP_SIGNED_SHORT:
        return struct.unpack("<h", reader.read(2))[0]
    if opcode == OP_LONG:
        return struct.unpack("<i", reader.read(4))[0]
    if opcode == OP_LONG_LONG:
        return reader.i64()
    if opcode == OP_VAR_INT:
        length = reader.size_ex()
        if length == 0:
            return 0
        raw = reader.read(length)
        return int.from_bytes(raw, "little", signed=False)
    if opcode == OP_ZERO_REAL:
        return 0.0
    if opcode == OP_REAL:
        return reader.double()
    if opcode == OP_EMPTY_STRING:
        return b""
    if opcode == OP_CHAR_STRING:
        return reader.read(1)
    if opcode == OP_SHORT_STRING:
        return reader.read(reader.u8())
    if opcode == OP_LONG_STRING:
        return reader.read(reader.size_ex())
    if opcode == OP_STRING_TABLE:
        index = reader.u8()
        if index < 1 or index > len(STRING_TABLE):
            raise MockError("string table index out of range")
        return STRING_TABLE[index - 1]
    if opcode == OP_EMPTY_WSTRING:
        return ""
    if opcode == OP_WSTRING_UTF8:
        return reader.read(reader.size_ex()).decode("utf-8")
    if opcode == OP_EMPTY_TUPLE:
        return ()
    if opcode == OP_ONE_TUPLE:
        return (decode_value(reader),)
    if opcode == OP_TWO_TUPLE:
        return (decode_value(reader), decode_value(reader))
    if opcode == OP_TUPLE:
        return tuple(decode_value(reader) for _ in range(reader.size_ex()))
    if opcode == OP_EMPTY_LIST:
        return []
    if opcode == OP_ONE_LIST:
        return [decode_value(reader)]
    if opcode == OP_LIST:
        return [decode_value(reader) for _ in range(reader.size_ex())]
    if opcode == OP_DICT:
        result = {}
        for _ in range(reader.size_ex()):
            value = decode_value(reader)
            key = decode_value(reader)
            result[key] = value
        return result
    if opcode == OP_OBJECT:
        name = decode_value(reader)
        args = decode_value(reader)
        return {"type": name, "args": args}
    if opcode == OP_BUFFER:
        return reader.read(reader.size_ex())
    raise MockError("unsupported opcode 0x%02X" % opcode)


def decode_stream(data):
    if data[0] != MARSHAL_HEADER:
        raise MockError("invalid marshal header")
    reader = Reader(data)
    reader.u8()
    reader.u32()
    return decode_value(reader)


class Writer:
    def __init__(self):
        self.chunks = []

    def u8(self, value):
        self.chunks.append(bytes([value & 0xFF]))

    def u32(self, value):
        self.chunks.append(struct.pack("<I", value))

    def i64(self, value):
        self.chunks.append(struct.pack("<q", value))

    def double(self, value):
        self.chunks.append(struct.pack("<d", value))

    def bytes(self, data):
        self.chunks.append(data)

    def size_ex(self, size):
        if size < 0xFF:
            self.u8(size)
        else:
            self.u8(0xFF)
            self.u32(size)

    def result(self):
        return b"".join(self.chunks)


def encode_integer(writer, value):
    if value == -1:
        writer.u8(OP_MINUS_ONE)
    elif value == 0:
        writer.u8(OP_ZERO_INT)
    elif value == 1:
        writer.u8(OP_ONE_INT)
    elif -128 <= value <= 127:
        writer.u8(OP_BYTE)
        writer.bytes(struct.pack("<b", value))
    elif -32768 <= value <= 32767:
        writer.u8(OP_SIGNED_SHORT)
        writer.bytes(struct.pack("<h", value))
    elif -2147483648 <= value <= 2147483647:
        writer.u8(OP_LONG)
        writer.bytes(struct.pack("<i", value))
    else:
        writer.u8(OP_LONG_LONG)
        writer.i64(value)


def encode_string(writer, value):
    if isinstance(value, str):
        value = value.encode("utf-8")
    if len(value) == 0:
        writer.u8(OP_EMPTY_STRING)
    elif len(value) == 1:
        writer.u8(OP_CHAR_STRING)
        writer.bytes(value)
    else:
        index = STRING_TO_INDEX.get(value.decode("utf-8", "ignore"), 0)
        if index:
            writer.u8(OP_STRING_TABLE)
            writer.u8(index)
        else:
            writer.u8(OP_LONG_STRING)
            writer.size_ex(len(value))
            writer.bytes(value)


def encode_value(writer, value):
    if value is None:
        writer.u8(OP_NONE)
    elif value is True:
        writer.u8(OP_TRUE)
    elif value is False:
        writer.u8(OP_FALSE)
    elif isinstance(value, int):
        encode_integer(writer, value)
    elif isinstance(value, float):
        if value == 0.0:
            writer.u8(OP_ZERO_REAL)
        else:
            writer.u8(OP_REAL)
            writer.double(value)
    elif isinstance(value, (str, bytes, bytearray)):
        if isinstance(value, str):
            encode_string(writer, value.encode("utf-8"))
        else:
            encode_string(writer, bytes(value))
    elif isinstance(value, tuple):
        if len(value) == 0:
            writer.u8(OP_EMPTY_TUPLE)
        elif len(value) == 1:
            writer.u8(OP_ONE_TUPLE)
        elif len(value) == 2:
            writer.u8(OP_TWO_TUPLE)
        else:
            writer.u8(OP_TUPLE)
            writer.size_ex(len(value))
        for item in value:
            encode_value(writer, item)
    elif isinstance(value, list):
        if len(value) == 0:
            writer.u8(OP_EMPTY_LIST)
        elif len(value) == 1:
            writer.u8(OP_ONE_LIST)
        else:
            writer.u8(OP_LIST)
            writer.size_ex(len(value))
        for item in value:
            encode_value(writer, item)
    elif isinstance(value, dict):
        writer.u8(OP_DICT)
        writer.size_ex(len(value))
        for key, item in value.items():
            encode_value(writer, item)
            encode_value(writer, key)
    else:
        raise MockError("unsupported value type: %r" % type(value))


def encode_stream(value):
    writer = Writer()
    writer.u8(MARSHAL_HEADER)
    writer.u32(0)
    encode_value(writer, value)
    return writer.result()


def encode_frame(payload):
    return struct.pack("<I", len(payload)) + payload


def recv_exact(connection, count):
    chunks = bytearray()
    while len(chunks) < count:
        chunk = connection.recv(count - len(chunks))
        if not chunk:
            raise ConnectionError("peer closed the connection early")
        chunks.extend(chunk)
    return bytes(chunks)


def read_frame(connection):
    header = recv_exact(connection, 4)
    length = struct.unpack("<I", header)[0]
    if length > MAX_FRAME_BYTES:
        raise ConnectionError("frame exceeds the fixture bound")
    return recv_exact(connection, length)


def send_value(connection, value):
    connection.sendall(encode_frame(encode_stream(value)))


def handle_handshake(connection):
    version_server = (
        170472, 496, 0, 24.01, 3396210, "V24.01@ccp", None,
    )
    send_value(connection, version_server)

    version_client = decode_stream(read_frame(connection))
    if not isinstance(version_client, tuple) or len(version_client) < 6:
        raise ConnectionError("invalid version exchange client")

    command = decode_stream(read_frame(connection))
    if not isinstance(command, tuple) or len(command) != 3:
        raise ConnectionError("invalid VK command")

    crypto_request = decode_stream(read_frame(connection))
    if not isinstance(crypto_request, tuple) or len(crypto_request) != 2:
        raise ConnectionError("invalid crypto request")

    send_value(connection, "OK CC")

    challenge = decode_stream(read_frame(connection))
    if not isinstance(challenge, tuple) or len(challenge) != 2:
        raise ConnectionError("invalid crypto challenge")

    send_value(connection, 2)
    server_handshake = (
        "",
        (b"\x74\x04\x00\x00\x00\x4e\x6f\x6e\x65", False),
        {},
        {
            "challenge_responsehash": "55087",
            "macho_version": 496,
            "boot_version": 24.01,
            "boot_build": 3396210,
            "boot_codename": "Ithax",
            "boot_region": "ccp",
            "cluster_usercount": 0,
            "proxy_nodeid": 0xFFAA,
            "user_logonqueueposition": 1,
            "config_vals": {},
        },
    )
    send_value(connection, server_handshake)

    handshake_result = decode_stream(read_frame(connection))
    if not isinstance(handshake_result, tuple) or len(handshake_result) != 3:
        raise ConnectionError("invalid handshake result")

    ack = {
        "live_updates": [],
        "session_init": {
            "languageID": "EN",
            "userid": 1,
            "maxSessionTime": None,
            "userType": 30,
            "role": 0,
            "address": "127.0.0.1",
            "inDetention": None,
        },
        "sessionID": 1,
        "client_hash": None,
        "user_clientid": 1,
    }
    send_value(connection, ack)
    print(json.dumps({"event": "stage4_mock_handshake_complete"}), flush=True)


def handle_session(connection):
    """Serve one post-handshake session: answer pings, then close."""
    while True:
        try:
            payload = read_frame(connection)
        except (ConnectionError, OSError):
            return
        try:
            value = decode_stream(payload)
        except MockError:
            return
        if isinstance(value, tuple) and len(value) >= 1:
            first = value[0]
            if isinstance(first, int) and first == 20:
                send_value(connection, "OK")
                print(
                    json.dumps({"event": "stage4_mock_ping_answered"}),
                    flush=True,
                )
                return


def serve(listener):
    while True:
        try:
            connection, _address = listener.accept()
        except OSError:
            return
        connection.settimeout(SOCKET_TIMEOUT_SECONDS)
        try:
            handle_handshake(connection)
            handle_session(connection)
        except (ConnectionError, OSError, MockError) as error:
            print(
                json.dumps(
                    {"event": "stage4_mock_rejected", "error": str(error)}
                ),
                flush=True,
            )
        finally:
            connection.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Loopback mock server for the Stage 4 network gate"
    )
    parser.add_argument("--port", type=int, default=26001)
    args = parser.parse_args()

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind((LOOPBACK_ADDRESS, args.port))
    listener.listen(LISTEN_BACKLOG)
    actual_port = listener.getsockname()[1]
    print(
        json.dumps(
            {"event": "stage4_mock_ready", "port": actual_port}
        ),
        flush=True,
    )
    thread = threading.Thread(target=serve, args=(listener,), daemon=True)
    thread.start()
    try:
        while thread.is_alive():
            thread.join(timeout=1.0)
    except KeyboardInterrupt:
        pass
    finally:
        listener.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
