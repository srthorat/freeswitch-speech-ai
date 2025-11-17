#!/bin/bash
set -e

echo "========================================="
echo "mod_audio_fork Verification Script"
echo "========================================="
echo ""

IMAGE_NAME=${1:-freeswitch-mod-audio-fork:latest}

echo "Using image: $IMAGE_NAME"
echo ""

echo "Step 1: Checking module file exists in image..."
docker run --rm $IMAGE_NAME ls -lh /usr/local/freeswitch/mod/mod_audio_fork.so
echo "✅ Module file exists"
echo ""

echo "Step 2: Checking module dependencies..."
docker run --rm $IMAGE_NAME ldd /usr/local/freeswitch/mod/mod_audio_fork.so | grep -i websockets
echo "✅ Module linked with libwebsockets"
echo ""

echo "Step 3: Starting FreeSWITCH and checking module loading..."
docker run --rm $IMAGE_NAME bash -c '
    echo "Starting FreeSWITCH in background..."
    /usr/local/freeswitch/bin/freeswitch -nc -nf > /dev/null 2>&1 &
    FS_PID=$!
    
    echo "Waiting for FreeSWITCH to start (15 seconds)..."
    sleep 15
    
    echo ""
    echo "Checking FreeSWITCH log for mod_audio_fork..."
    if grep -i "mod_audio_fork" /usr/local/freeswitch/log/freeswitch.log; then
        echo ""
        echo "✅ mod_audio_fork found in logs!"
    else
        echo ""
        echo "❌ mod_audio_fork NOT found in logs"
        exit 1
    fi
    
    echo ""
    echo "Checking for loading errors..."
    if grep -i "mod_audio_fork" /usr/local/freeswitch/log/freeswitch.log | grep -iE "error|fail"; then
        echo "❌ Errors detected when loading mod_audio_fork"
        exit 1
    else
        echo "✅ No errors detected"
    fi
    
    kill $FS_PID 2>/dev/null || true
'

echo ""
echo "========================================="
echo "✅ VERIFICATION COMPLETE!"
echo "========================================="
echo "mod_audio_fork is installed and loads successfully!"
echo ""
echo "To start FreeSWITCH with mod_audio_fork:"
echo "  docker run --rm -it $IMAGE_NAME freeswitch -nc -nf"
echo ""
