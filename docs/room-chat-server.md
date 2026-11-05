# Room text chat server contract

The desktop client adds public in-room text chat in protocol packet `10020`.
This is a transient room feature: the server does not need to store messages in a
database and clients do not request history after joining.

## Packet layout

Use the existing TCP outer framing: send one 4-byte `int payloadLength`, then
the complete packet body. The packet body is always 528 bytes.

```cpp
#define DEF_PACK_ROOM_CHAT_MESSAGE (DEF_PACK_BASE + 20) // 10020
#define DEF_ROOM_CHAT_MAX_BYTES 512

typedef struct STRU_ROOM_CHAT_MESSAGE
{
    STRU_ROOM_CHAT_MESSAGE()
        : m_nType(DEF_PACK_ROOM_CHAT_MESSAGE), m_RoomID(0), m_UserID(0),
          m_messageLength(0)
    {
        memset(m_message, 0, sizeof(m_message));
    }
    PackType m_nType;
    int m_RoomID;
    int m_UserID;
    int m_messageLength;
    char m_message[DEF_ROOM_CHAT_MAX_BYTES];
} STRU_ROOM_CHAT_MESSAGE;

static_assert(sizeof(STRU_ROOM_CHAT_MESSAGE) == 528, "chat packet layout");
```

Keep the project's default structure alignment. Do not use `#pragma pack(1)`.
`m_message` contains exactly `m_messageLength` bytes of UTF-8 text and does not
need a trailing `\0`.

## Handler requirements

Register `10020` in the main control-connection packet dispatch table.

1. Accept it only from an authenticated main control TCP connection. Never
   process it on audio/video registration sockets.
2. Require `length == sizeof(STRU_ROOM_CHAT_MESSAGE)`, `1 <= m_messageLength
   <= 512`, and a valid UTF-8 payload.
3. Ignore the user ID supplied by the client for identity. Compare it with the
   authenticated user bound to the source control connection; reject the packet
   if it differs. Before relaying, set `m_UserID` from that authenticated user.
4. Require that `m_RoomID` is the room currently bound to the source user and
   that the room is nonzero. Reject stale-room or cross-room messages.
5. Limit each user to a small burst and sustained rate, for example four
   messages in two seconds and no more than 30 messages per minute. Drop
   over-limit packets without broadcasting them.
6. Relay the validated, normalized 528-byte packet to every *other* current
   participant's main control TCP connection in that room. Do not echo it to
   the source connection because the client displays its own message after a
   successful local write.

The existing `SendData` helper should preserve the normal outer length frame;
do not split the protocol header and text into separate business packets. Do
not send chat packets to audio/video sockets. Old clients may ignore unknown
packet type `10020` safely.

## Expected behavior

Two authenticated users in the same room can exchange text immediately. New
participants begin with an empty timeline, and leaving a room clears that
client's local timeline. The server does not retain or replay chat history.
