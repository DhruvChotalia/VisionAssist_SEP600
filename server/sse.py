import queue
import threading
import json
from flask import Response

_sse_clients: list[queue.Queue] = []
_sse_lock = threading.Lock()


def broadcast_sse(event_type: str, data: dict):
    """Push a Server-Sent Event to every connected dashboard client."""
    msg = f"event: {event_type}\ndata: {json.dumps(data)}\n\n"
    with _sse_lock:
        dead = []
        for q in _sse_clients:
            try:
                q.put_nowait(msg)
            except queue.Full:
                dead.append(q)
        for q in dead:
            _sse_clients.remove(q)


def stream_response() -> Response:
    """Register a new SSE client and return a streaming Response."""
    q = queue.Queue(maxsize=50)
    with _sse_lock:
        _sse_clients.append(q)

    def event_stream():
        try:
            while True:
                yield q.get()
        except GeneratorExit:
            with _sse_lock:
                if q in _sse_clients:
                    _sse_clients.remove(q)

    return Response(
        event_stream(),
        mimetype='text/event-stream',
        headers={'Cache-Control': 'no-cache', 'X-Accel-Buffering': 'no'}
    )
