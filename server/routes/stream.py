from flask import Blueprint
from sse import stream_response

stream_bp = Blueprint('stream', __name__)


@stream_bp.route('/stream')
def stream():
    return stream_response()