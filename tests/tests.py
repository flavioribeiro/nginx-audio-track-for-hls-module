#encoding:utf-8

import pytest
import requests
import commands
from tempfile import NamedTemporaryFile
import os

def test_request_aac_should_demux_from_ts():
    aac_file = NamedTemporaryFile()
    response = requests.get("http://localhost:8080/segment.aac")
    aac_file.write(response.content)
    mediainfo_output = commands.getoutput("mediainfo " + aac_file.name)

    assert response.status_code == 200
    assert "Advanced Audio Codec" in mediainfo_output and "Video" not in mediainfo_output
    assert os.path.exists(aac_file.name)

def test_request_aac_should_return_audio_aac_on_mime_type():
    response = requests.get("http://localhost:8080/tvg.aac")
    assert response.headers['content-type'] == "audio/aac"

def test_request_aac_from_invalid_ts_should_return_40x():
    response = requests.get("http://localhost:8080/this_chunk_doesnt_exist.aac")
    assert response.status_code == 404
    # improvement (issue #18)
    #assert "text/plain" == response.headers['content-type']
    #assert "the ts associated to aac requested does not exist" == response.text

def test_request_aac_from_ts_without_aac_should_return_40x():
    pytest.skip("Not implemented yet. Issue #19")

def test_request_aac_from_ts_with_mp3_should_return_500():
    pytest.skip("Not implemented yet. Issue #20")

