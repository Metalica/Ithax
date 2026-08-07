# machoNet Protocol Specification (Base)

> Version: 1.0.0
>
> Status: Verified against the approved server-side reference (EVE 24.01
> build 3396210 profile) and the EVEmu protocol sources on 2026-08-07.
>
> Scope: the base game-socket protocol only. Protobuf/gRPC (Quasar) paths,
> HTTPS endpoints, and proposed Ithax relevancy extensions are separate.

## 1. Conformance Pins

| Pin | Value |
|-----|-------|
| Client compatibility profile | EVE 24.01, build 3396210 |
| `eveBirthday` | 170472 |
| `machoVersion` | 496 |
| `projectVersion` | `V24.01@ccp` |
| Server port (default) | 26000, loopback-only deployment profile |
| Crypto pack | `Placebo` (AES-256-CBC session) |
| Reference implementations | EVEmu `evemu_server` (LGPL-2.1) and the approved
  server-side reference (AGPL-3.0, separate component) |

The approved server-side reference is a separate AGPL-3.0 component and is
not part of this repository or its distribution artifacts. This document is
a behavioral specification derived from those sources; no AGPL code is
copied into the client.

## 2. Transport and Framing

- TCP, one byte stream per session.
- Frame shape:

```
[4-byte LE payload length][payload]
payload = [0x7E marshal header][4-byte LE map count][marshaled value]
```

- The length prefix counts only the payload bytes (it excludes the 4-byte
  header itself).
- `map count` is always 0 in the base protocol (object-store references are
  not used by the approved server).
- **Packet size limit: 1 MiB (1,048,576 bytes) per frame, both directions.**
  Frames exceeding the limit are rejected.
- **Deflation:** when a marshaled payload is at least 8192 bytes, the
  payload is zlib-deflated before framing; smaller payloads are sent raw.
  Inbound payloads are checked for the zlib header and inflated when
  present.
- Partial frames are buffered until complete; multiple frames may be
  coalesced in one TCP segment.
- Idle timeout: 10 minutes.

## 3. EVE Marshal Value Format

### 3.1 Opcode Table

| Opcode | Name | Payload |
|--------|------|---------|
| 0x01 | `PyNone` | — |
| 0x02 | `PyToken` | SizeEx + bytes |
| 0x03 | `PyLongLong` | int64 LE |
| 0x04 | `PyLong` | int32 LE |
| 0x05 | `PySignedShort` | int16 LE |
| 0x06 | `PyByte` | int8 |
| 0x07 | `PyMinusOne` | — |
| 0x08 | `PyZeroInteger` | — |
| 0x09 | `PyOneInteger` | — |
| 0x0A | `PyReal` | double LE |
| 0x0B | `PyZeroReal` | — |
| 0x0D | `PyBuffer` | SizeEx + bytes |
| 0x0E | `PyEmptyString` | — |
| 0x0F | `PyCharString` | 1 byte |
| 0x10 | `PyShortString` | uint8 len + bytes |
| 0x11 | `PyStringTableItem` | uint8 index (1-based) |
| 0x12 | `PyWStringUCS2` | SizeEx + UTF-16LE code units |
| 0x13 | `PyLongString` | SizeEx + bytes |
| 0x14 | `PyTuple` | SizeEx count + elements |
| 0x15 | `PyList` | SizeEx count + elements |
| 0x16 | `PyDict` | SizeEx count + value/key pairs |
| 0x17 | `PyObject` | type string + args value |
| 0x19 | `PySubStruct` | one nested value |
| 0x1B | `PySavedStreamElement` | SizeEx index (1-based) |
| 0x1C | `PyChecksumedStream` | uint32 checksum + value |
| 0x1F | `PyTrue` | — |
| 0x20 | `PyFalse` | — |
| 0x21 | `cPicked` | SizeEx + pickle bytes |
| 0x22 | `PyObjectEx1` | header + list + terminator + dict + terminator |
| 0x23 | `PyObjectEx2` | header + list + terminator + dict + terminator |
| 0x24 | `PyEmptyTuple` | — |
| 0x25 | `PyOneTuple` | 1 element |
| 0x26 | `PyEmptyList` | — |
| 0x27 | `PyOneList` | 1 element |
| 0x28 | `PyEmptyWString` | — |
| 0x29 | `PyWStringUCS2Char` | 2 bytes |
| 0x2A | `PyPackedRow` | header + RLE blob + size-0 columns |
| 0x2B | `PySubStream` | SizeEx + full inner marshal stream |
| 0x2C | `PyTwoTuple` | 2 elements |
| 0x2D | `PackedTerminator` | — |
| 0x2E | `PyWStringUTF8` | SizeEx + UTF-8 bytes |
| 0x2F | `PyVarInteger` | SizeEx len + LE bytes |

Header byte flags: opcode mask `0x3F`, save flag `0x40`, unknown flag
`0x80`. The base protocol never sets the save flag (map count is 0).

### 3.2 SizeEx

- `size < 0xFF`: one byte.
- `size >= 0xFF`: `0xFF` followed by uint32 LE.

### 3.3 Integer Encoding

| Value | Encoding |
|-------|----------|
| -1 | `PyMinusOne` |
| 0 | `PyZeroInteger` |
| 1 | `PyOneInteger` |
| int8 range | `PyByte` + int8 |
| int16 range | `PySignedShort` + int16 LE |
| int32 range | `PyLong` + int32 LE |
| int64, high bytes 5-7 zero | `PyVarInteger` + size + LE bytes |
| otherwise | `PyLongLong` + int64 LE |

### 3.4 String Encoding

- Empty: `PyEmptyString`.
- One byte: `PyCharString` + byte.
- Otherwise: string-table lookup (djb2 hash, 1-based index); on hit
  `PyStringTableItem` + index, else `PyLongString` + SizeEx + bytes.
- Wide strings: empty `PyEmptyWString`, else `PyWStringUTF8` + SizeEx +
  UTF-8 bytes.

### 3.5 Container Encoding

- Tuple: 0/1/2 elements use `PyEmptyTuple`/`PyOneTuple`/`PyTwoTuple`;
  otherwise `PyTuple` + SizeEx count.
- List: 0/1 elements use `PyEmptyList`/`PyOneList`; otherwise `PyList` +
  SizeEx count.
- Dict: `PyDict` + SizeEx count, then for each entry **value first, then
  key** (reversed from the natural order).
- Object: `PyObject` + type string + args.
- ObjectEx: opcode, header, list elements, `PackedTerminator`, dict
  key/value pairs, `PackedTerminator`.
- SubStream: `PySubStream` + SizeEx + a complete inner marshal stream
  (0x7E + map count + value).

### 3.6 Packed Rows

- `PyPackedRow` + DBRowDescriptor header + SizeEx RLE length + RLE bytes,
  then size-0 columns (STR/WSTR/BYTES) marshaled as values.
- DBRowDescriptor: `PyObjectEx1` with token `blue.DBRowDescriptor` and a
  column list; each column is `(name, DBTYPE int)`.
- Fixed-size columns are packed in size order (largest first, then column
  index) in little-endian; BOOL columns are bit-packed; a null bitmap
  follows (one bit per column).
- RLE zero-compression: control bytes with two 4-bit nibbles; a nibble
  below 8 is a literal run of `8 - nibble` bytes, a nibble of 8 or more is
  a zero run of `nibble - 7` bytes.

### 3.7 DBTYPE

| Type | Value | Bits |
|------|-------|------|
| EMPTY | 0x00 | 0 |
| I2 | 0x02 | 16 |
| I4 | 0x03 | 32 |
| R4 | 0x04 | 32 |
| R8 | 0x05 | 64 |
| CY | 0x06 | 64 |
| ERROR | 0x0A | 0 |
| BOOL | 0x0B | 1 |
| I1 | 0x10 | 8 |
| UI1 | 0x11 | 8 |
| UI2 | 0x12 | 16 |
| UI4 | 0x13 | 32 |
| I8 | 0x14 | 64 |
| UI8 | 0x15 | 64 |
| FILETIME | 0x40 | 64 |
| BYTES | 0x80 | 0 |
| STR | 0x81 | 0 |
| WSTR | 0x82 | 0 |

## 4. MachoNet Packet Structure

A machoNet packet is a `PyObject` whose type string is the packet class
and whose args are a 7-tuple:

```
[0] message type (int)
[1] source address
[2] destination address
[3] user id (int or None)
[4] payload (tuple)
[5] named payload (dict or None)
[6] None
```

Message types: `AUTHENTICATION_REQ=0`, `AUTHENTICATION_RSP=1`,
`IDENTIFICATION_REQ=2`, `IDENTIFICATION_RSP=3`, `CALL_REQ=6`,
`CALL_RSP=7`, `TRANSPORTCLOSED=8`, `RESOLVE_REQ=10`, `RESOLVE_RSP=11`,
`NOTIFICATION=12`, `ERRORRESPONSE=15`, `SESSIONCHANGENOTIFICATION=16`,
`SESSIONINITIALSTATENOTIFICATION=18`, `PING_REQ=20`, `PING_RSP=21`.

Addresses are `PyObject("macho.MachoAddress", tuple)`:

| Type | Value | Tuple |
|------|-------|-------|
| Node | 1 | `[type, nodeID, service, callID]` |
| Client | 2 | `[type, clientID, callID, service]` |
| Broadcast | 4 | `[type, broadcastID, narrowcast, idtype]` |
| Any | 8 | `[type, service, callID]` |

Call requests (`macho.CallReq`) carry a substream whose inner tuple is
`[remoteObject (int or string), method (string), arg_tuple, arg_dict or
None]`, wrapped as `[[flag, substream], None]`.

## 5. Session Handshake (PLACEBO)

Client states: `WAIT_VERSION → WAIT_COMMAND → WAIT_CRYPTO → WAIT_AUTH →
WAIT_FUNC_RESULT → SESSION`.

| Step | Direction | Message |
|------|-----------|---------|
| 1 | S → C | `VersionExchangeServer` tuple: `[birthday, macho_version, user_count, version_number (real), build_version, project_version, update_info (None)]` |
| 2 | C → S | `VersionExchangeClient` tuple (same, without `update_info`) |
| 3 | C → S | VK command tuple `(None, "VK", vipKey)`; or QC tuple `(None, "QC")` which is answered with `PyInt(queuePos)` and restarts at step 1 |
| 4 | C → S | `CryptoRequestPacket` tuple `[keyVersion, keyParams dict]`; S replies `"OK CC"` |
| 5 | C → S | `CryptoChallengePacket` tuple `[clientChallenge, login dict]`; S replies `PyInt(2)` then `CryptoServerHandshake` tuple `[serverChallenge, (marshaled_code, verification), context, response dict]` |
| 6 | C → S | `CryptoHandshakeResult` tuple `[challenge_responsehash, func_output, func_result]`; S replies `CryptoHandshakeAck` dict with `session_init`, `sessionID`, `user_clientid` |

Login dict keys: `user_name` (wstring), `user_password_hash` (string),
`user_languageid` (wstring), `user_affiliateid` (int), `macho_version`,
`boot_version`, `boot_build`, `boot_codename`, `boot_region`.

## 6. Session Crypto

- AES-256-CBC, 32-byte key, 16-byte IV, PKCS#7 padding.
- The key and IV are supplied by the client in the `CryptoRequestPacket`
  key params (`crypting_sessionkey`, `crypting_sessioniv`); the server
  trims the key to 32 bytes and the IV to 16 bytes.
- When no IV is supplied, a zero-filled 16-byte IV is used.
- CBC chaining: after each operation the next IV is the last 16 bytes of
  the previous ciphertext.
- Encryption applies to the payload only; the frame is re-written with the
  encrypted payload length.
- **CBC alone is not authenticated encryption.** This legacy compatibility
  mode is not equivalent to TLS/AEAD. For untrusted remote networks a
  separately reviewed secure transport or a versioned protocol upgrade is
  required.
- Transient key material is zeroed where practical; credentials, plaintext
  authentication payloads, keys, and tokens are never logged.

## 7. Limits and Malformed-Input Policy

| Limit | Value |
|-------|-------|
| Frame payload | 1 MiB |
| Marshal nesting depth | 64 |
| Container element count | 65,536 |
| String/buffer length | 1 MiB |
| String-table index | 1..195 |
| Handshake step timeout | 30 s |

Malformed input is rejected with a typed error and the connection is
closed. Rejections cover: bad header byte, truncated data, unknown
opcodes, out-of-range string-table indices, invalid saved-element
references, excessive nesting, excessive allocation, duplicate forbidden
fields, and trailing data after the root value.

## 8. Compatibility Policy

- The base protocol above is the verified contract for the approved
  server-side reference.
- Proposed Ithax relevancy extensions (`EntityEnter`, `EntityLeave`,
  `EntityDelta`, `RelevancySnapshot`) are a versioned server-side
  extension and are out of scope for the base gate.
- Unknown message types are rejected; unknown service methods are routed
  to the typed dispatch boundary and reported, never silently dropped.

## 9. Client Runtime Behavior

- **Connection generations:** every connect increments a generation
  counter; decoded packets carry the generation they were received under,
  and messages from a stale generation are rejected.
- **Heartbeat:** the client sends `macho.PingReq` on a configurable
  interval (default 30 s) and answers server `PingReq` with `PingRsp`.
- **Reconnect:** connect failures retry with a bounded policy (default 3
  attempts, 1 s delay); each attempt establishes a fresh session.
- **Typed dispatch:** decoded `CALL_REQ` packets are dispatched to
  registered service/method handlers; `CALL_RSP`, `NOTIFICATION`,
  `ERRORRESPONSE`, `TRANSPORTCLOSED`, and session-change packets are
  routed to typed handlers.
- **Loopback-only policy:** the client network path rejects any
  non-loopback destination by default.

## 10. RPC Inventory

The verified service catalog (201 service files, 1,690 `Handle_*` methods
across 70 categories) is documented in `docs/protocol/rpc-inventory.md`.
The base client surface is `machoNet.GetInitVals` / `GetGlobalConfig` /
`GetServerStatus`, `authentication.Login`, `char.GetCharactersToSelect` /
`SelectCharacterID`, `macho.CallReq`/`CallRsp`, `macho.PingReq`/`PingRsp`,
`macho.Notification`, `macho.ErrorResponse`, and
`macho.TransportClosed`.
