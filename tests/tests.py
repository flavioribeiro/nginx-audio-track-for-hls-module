#encoding:utf-8

import requests
import commands
from tempfile import NamedTemporaryFile
import os

def test_request_aac_should_demux_from_ts():
    aac_file = NamedTemporaryFile()
    response = requests.get("http://localhost:8080/segment.aac")
    aac_file.write(response.content)
    mediainfo_output = commands.getoutput("mediainfo " + aac_file.name)

    assert 200 == response.status_code
    assert "Advanced Audio Codec" in mediainfo_output and "Video" not in mediainfo_output
    assert os.path.exists(aac_file.name)


