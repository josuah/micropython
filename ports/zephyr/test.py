def print_caps(caps):
    i = 0
    while True:
        caps = e0.caps(0, i)
        if caps is None:
            break
        i += 1
        print(caps)

def callback(buffer):
    print(f'hello world {buffer}')

from zephyr.mediapipe import *

e0 = Element('vid_src')
e1 = Element('appsink')
pipeline = Pipeline(e0, e1)
pipeline.link(e0, e1)

e0.prop(PROP_VID_DEVICE, 'video-sw-generator')
e1.prop(PROP_APPSINK_HOOK, callback)

pipeline.state(STATE_PLAYING)
pipeline.state(STATE_PAUSED)

###
e0.caps(0, 0)
e1.caps(0, 0)

###
from zephyr.mediapipe import *
import gc
e0 = Element('zvid_src')
e0.__del__()
del e0
gc.collect()

###
from zephyr.mediapipe import *
Element('zvid_src').__del__()
print(heap_stats())
