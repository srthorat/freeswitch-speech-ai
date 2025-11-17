# Docker Hub Deployment and MacBook Testing Guide

This guide walks you through pushing the FreeSWITCH base image to Docker Hub and testing it on your MacBook with SIP calling between extensions 1000 and 1001.

---

## Prerequisites

1. **Docker Hub Account**: Sign up at https://hub.docker.com if you don't have one
2. **Docker installed on MacBook**: Download from https://www.docker.com/products/docker-desktop
3. **SIP Client**: Install a softphone on your MacBook (recommended options below)

---

## Part 1: Build and Push to Docker Hub (Development Environment)

### Step 1: Build the Image

```bash
# In your development environment
cd /workspaces/freeswitch_modules

# Build the image (takes 30-45 minutes)
./dockerfiles/build-freeswitch-base.sh freeswitch-base:1.10.11
```

Wait for the build to complete. You should see:
```
✅ FreeSWITCH configured
✅ FreeSWITCH compiled
✅ FreeSWITCH installed
✅ Sounds installed
✅ Sample configuration installed
```

### Step 2: Tag the Image for Docker Hub

Replace `YOUR_DOCKERHUB_USERNAME` with your actual Docker Hub username:

```bash
# Tag the image
docker tag freeswitch-base:1.10.11 YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11

# Also tag as latest
docker tag freeswitch-base:1.10.11 YOUR_DOCKERHUB_USERNAME/freeswitch-base:latest
```

**Example**:
```bash
docker tag freeswitch-base:1.10.11 johndoe/freeswitch-base:1.10.11
docker tag freeswitch-base:1.10.11 johndoe/freeswitch-base:latest
```

### Step 3: Login to Docker Hub

```bash
docker login
```

Enter your Docker Hub username and password when prompted.

**Alternative** (using access token for better security):
```bash
# Create access token at: https://hub.docker.com/settings/security
docker login -u YOUR_DOCKERHUB_USERNAME
# Paste access token when prompted for password
```

### Step 4: Push to Docker Hub

```bash
# Push the versioned tag
docker push YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11

# Push the latest tag
docker push YOUR_DOCKERHUB_USERNAME/freeswitch-base:latest
```

**Note**: This will take 5-15 minutes depending on your upload speed. The image is ~800 MB - 1 GB.

You'll see output like:
```
The push refers to repository [docker.io/YOUR_DOCKERHUB_USERNAME/freeswitch-base]
abc123def456: Pushed
789ghi012jkl: Pushed
...
1.10.11: digest: sha256:xxxxx size: 1234
```

### Step 5: Verify on Docker Hub

Visit https://hub.docker.com/r/YOUR_DOCKERHUB_USERNAME/freeswitch-base to confirm the image is published.

---

## Part 2: Pull and Run on MacBook

### Step 1: Open Terminal on MacBook

```bash
# Verify Docker is running
docker --version

# Should show something like: Docker version 24.x.x, build xxxxx
```

### Step 2: Pull the Image from Docker Hub

```bash
docker pull YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

This will download the image to your MacBook (5-10 minutes depending on internet speed).

### Step 3: Run the FreeSWITCH Container

```bash
docker run -d \
    --name freeswitch \
    --platform linux/amd64 \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 5080:5080/udp \
    -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

**Port Mapping Explanation**:
- `5060` - SIP signaling (TCP and UDP)
- `5080` - SIP over WebSocket (for WebRTC)
- `8021` - Event Socket (for fs_cli)
- `16384-16484` - RTP media (audio/video)

### Step 4: Verify FreeSWITCH is Running

```bash
# Check container is running
docker ps | grep freeswitch

# Check logs
docker logs freeswitch

# Should see FreeSWITCH startup messages
```

### Step 5: Connect with fs_cli

```bash
docker exec -it freeswitch fs_cli

docker exec -it freeswitch fs_cli -x "sofia status"
==>
                     Name          Type                                       Data      State
=================================================================================================
            external-ipv6       profile                   sip:mod_sofia@[::1]:5080      RUNNING (0)
               172.17.0.2         alias                                   internal      ALIASED
                 external       profile            sip:mod_sofia@20.192.21.57:5080      RUNNING (0)
    external::example.com       gateway                    sip:joeuser@example.com      NOREG
            internal-ipv6       profile                   sip:mod_sofia@[::1]:5060      RUNNING (0)
                 internal       profile              sip:mod_sofia@172.17.0.2:5060      RUNNING (0)
=================================================================================================

docker exec -it freeswitch cat  /usr/local/freeswitch/conf/sip_profiles/internal.xml | grep ext-rtp-ip
==>  <param name="ext-rtp-ip" value="$${local_ip_v4}"/>
```

You should see the FreeSWITCH CLI prompt:
```
 _____              ______        _____ _______ _____ _    _
|  ___| __ ___  ___/ ___\ \      / /_ _|_   _/ ____| |  | |
| |_ | '__/ _ \/ _ \___ \\ \ /\ / / | |  | || |    | |  | |
|  _|| | |  __/  __/___) |\ V  V /  | |  | || |____| |__| |
|_|  |_|  \___|\___|____/  \_/\_/  |___| |_| \_____|\____/

+OK
freeswitch@internal>
```

**Test commands**:
```
status              # Show FreeSWITCH status
sofia status        # Show SIP profiles
show modules        # List loaded modules (should be 100+)
show channels       # Show active calls
```

Type `exit` or press Ctrl+D to exit fs_cli.

---

## Part 3: Install SIP Client on MacBook

Choose one of these SIP clients for testing:

### Option A: Zoiper (Recommended - Free)

1. Download: https://www.zoiper.com/en/voip-softphone/download/current
2. Install Zoiper 5 for macOS
3. Launch Zoiper

### Option B: Linphone (Open Source - Free)

1. Download: https://www.linphone.org/releases/macosx/app/Linphone-4.x.x.dmg
2. Install and launch

### Option C: Telephone (Mac Native - Free)

1. Install from App Store or https://www.64characters.com/telephone/
2. Launch Telephone

### Option D: Bria (Professional - Paid)

1. Download: https://www.counterpath.com/bria-solo/
2. Free trial available

---

## Part 4: Configure SIP Clients for Extension 1000

### Get Your MacBook's IP Address

```bash
# Get container IP (use this if testing on same MacBook)
docker inspect freeswitch | grep IPAddress

# Or use localhost
SERVER_IP=localhost
```

### Configure Extension 1000 in Zoiper

**Account Settings**:
- Account name: `Extension 1000`
- SIP Username: `1000`
- SIP Password: `1234`
- SIP Domain/Host: `localhost` (or MacBook IP if testing from another device)
- SIP Port: `5060`
- Transport: `UDP`

**Detailed Steps (Zoiper)**:
1. Open Zoiper
2. Settings → Accounts → Add Account
3. Select "SIP"
4. Enter credentials above
5. Click "Create Account"
6. Status should show "Registered" (green)

### Configure Extension 1001 (for Second Client)

You have two options:

**Option A: Install second SIP client on MacBook**
- Use a different SIP client (e.g., if using Zoiper for 1000, use Linphone for 1001)
- Configure with username `1001`, password `1234`

**Option B: Use mobile phone**
- Install Zoiper/Linphone on your iPhone/Android
- Connect to same WiFi network as MacBook
- Use MacBook's local IP address (find with `ifconfig en0 | grep inet`)
- Configure with username `1001`, password `1234`

---

## Part 5: Test Calling Between Extensions

### From Extension 1000 to 1001

1. **On Extension 1000 client**:
   - Dial: `1001`
   - Press Call button
   - You should hear ringing

2. **On Extension 1001 client**:
   - Incoming call should appear
   - Answer the call

3. **Verify**:
   - Both extensions should hear each other
   - Talk to confirm two-way audio

### From Extension 1001 to 1000

1. **On Extension 1001 client**:
   - Dial: `1000`
   - Press Call button

2. **On Extension 1000 client**:
   - Answer incoming call

3. **Verify two-way audio**

### Monitor Calls in fs_cli

While calls are active:

```bash
docker exec -it freeswitch fs_cli
```

Then run:
```
freeswitch@internal> show channels

uuid,direction,created,created_epoch,name,state,cid_name,cid_num,ip_addr,dest,application,application_data,dialplan,context,read_codec,read_rate,read_bit_rate,write_codec,write_rate,write_bit_rate,secure,hostname,presence_id,presence_data,accountcode,callstate,callee_name,callee_num,callee_direction,call_uuid,sent_callee_name,sent_callee_num,initial_cid_name,initial_cid_num,initial_ip_addr,initial_dest,initial_dialplan,initial_context

1 total.
```

**Other useful monitoring commands**:
```
sofia status profile internal
show calls
uuid_dump <call-uuid>
```

---

## Part 6: Advanced Testing

### Test Echo Service

FreeSWITCH includes a built-in echo test:

1. From any registered extension, dial: `9196`
2. Speak into the microphone
3. You should hear your own voice echoed back (with slight delay)
4. This confirms audio path is working correctly

### Test Conference

1. From extension 1000, dial: `3000` (default conference room)
2. You'll hear "You are the only person in this conference"
3. From extension 1001, dial: `3000`
4. Both extensions should now be in conference bridge
5. Test multi-party audio

### Test Voicemail

1. From extension 1000, dial: `*98`
2. When prompted, enter: `1000#` (mailbox) then `1234#` (password)
3. Follow prompts to record a message

### Check Call Detail Records

```bash
docker exec -it freeswitch fs_cli -x "show calls"
```

---

## Part 7: Troubleshooting

### Extension Won't Register

**Check 1: Container is running**
```bash
docker ps | grep freeswitch
# Should show STATUS as "Up X minutes"
```

**Check 2: SIP profile is running**
```bash
docker exec -it freeswitch fs_cli -x "sofia status"
# Should show "internal" profile as RUNNING
```

**Check 3: Port 5060 is accessible**
```bash
# On MacBook
nc -zv localhost 5060
# Should show: Connection to localhost port 5060 [tcp/sip] succeeded!
```

**Check 4: Check SIP registration in fs_cli**
```bash
docker exec -it freeswitch fs_cli -x "sofia status profile internal reg"
# Should show registered users
```

### No Audio During Calls

**Issue**: Calls connect but no audio in one or both directions

**Solution 1: Check RTP ports**
```bash
# Make sure RTP ports are mapped
docker port freeswitch
# Should show 16384-16484/udp
```

**Solution 2: Firewall on MacBook**
- Go to System Preferences → Security & Privacy → Firewall
- Allow Docker to accept incoming connections

**Solution 3: NAT configuration**

If testing from external network, add to FreeSWITCH vars.xml:
```bash
docker exec -it freeswitch vim /usr/local/freeswitch/conf/vars.xml
```

Find and update:
```xml
<X-PRE-PROCESS cmd="set" data="external_rtp_ip=YOUR_PUBLIC_IP"/>
<X-PRE-PROCESS cmd="set" data="external_sip_ip=YOUR_PUBLIC_IP"/>
```

Then reload:
```bash
docker exec -it freeswitch fs_cli -x "reloadxml"
```

### Call Immediately Hangs Up

**Check dialplan**:
```bash
docker exec -it freeswitch fs_cli -x "xml_locate dialplan"
```

Verify extension 1001 exists in default dialplan.

### Container Keeps Restarting

**Check logs**:
```bash
docker logs freeswitch
```

Look for errors like:
- Configuration syntax errors
- Missing files
- Permission issues

### Can't Access fs_cli

**Check Event Socket is running**:
```bash
docker exec -it freeswitch netstat -an | grep 8021
# Should show: tcp  0  0 127.0.0.1:8021  0.0.0.0:*  LISTEN
```

---

## Part 8: Docker Commands Reference

### Container Management

```bash
# Start container
docker start freeswitch

# Stop container
docker stop freeswitch

# Restart container
docker restart freeswitch

# Remove container
docker rm -f freeswitch

# View logs (follow mode)
docker logs -f freeswitch

# View last 100 lines
docker logs --tail 100 freeswitch

# Execute command in container
docker exec -it freeswitch <command>

# Open bash shell in container
docker exec -it freeswitch bash
```

### Image Management

```bash
# List images
docker images | grep freeswitch

# Remove image
docker rmi YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11

# Pull latest version
docker pull YOUR_DOCKERHUB_USERNAME/freeswitch-base:latest

# Check image size
docker images YOUR_DOCKERHUB_USERNAME/freeswitch-base --format "{{.Repository}}:{{.Tag}}\t{{.Size}}"
```

### Network Debugging

```bash
# Check container IP
docker inspect freeswitch | grep IPAddress

# Check port mappings
docker port freeswitch

# Monitor network traffic (requires tcpdump in container)
docker exec -it freeswitch tcpdump -i any port 5060 -n
```

---

## Part 9: Production Considerations

### Persist Configuration and Data

```bash
# Create volumes for persistence
docker volume create freeswitch-conf
docker volume create freeswitch-logs
docker volume create freeswitch-data

# Run with volumes
docker run -d \
    --name freeswitch \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 5080:5080/udp \
    -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    -v freeswitch-conf:/usr/local/freeswitch/conf \
    -v freeswitch-logs:/usr/local/freeswitch/log \
    -v freeswitch-data:/usr/local/freeswitch/storage \
    YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

### Resource Limits

```bash
# Run with resource constraints
docker run -d \
    --name freeswitch \
    --memory="2g" \
    --cpus="2" \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

### Environment Variables

```bash
# Override default settings
docker run -d \
    --name freeswitch \
    -e FREESWITCH_LOG_LEVEL=DEBUG \
    -e FREESWITCH_RTP_START=10000 \
    -e FREESWITCH_RTP_END=20000 \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

---

## Summary Checklist

### Development Environment
- [ ] Build image: `./dockerfiles/build-freeswitch-base.sh`
- [ ] Tag for Docker Hub: `docker tag freeswitch-base:1.10.11 USER/freeswitch-base:1.10.11`
- [ ] Login to Docker Hub: `docker login`
- [ ] Push to Docker Hub: `docker push USER/freeswitch-base:1.10.11`
- [ ] Verify on hub.docker.com

### MacBook
- [ ] Pull image: `docker pull USER/freeswitch-base:1.10.11`
- [ ] Run container with port mappings
- [ ] Verify with `docker ps` and `docker logs`
- [ ] Test fs_cli access

### SIP Testing
- [ ] Install SIP client(s) on MacBook
- [ ] Register extension 1000 (username: 1000, password: 1234)
- [ ] Register extension 1001 (username: 1001, password: 1234)
- [ ] Test call from 1000 to 1001
- [ ] Test call from 1001 to 1000
- [ ] Verify two-way audio
- [ ] Test echo service (dial 9196)
- [ ] Test conference (dial 3000)

### Monitoring
- [ ] Monitor calls in fs_cli: `show channels`
- [ ] Check SIP registrations: `sofia status profile internal reg`
- [ ] View call logs in FreeSWITCH logs

---

## Next Steps

Once calling is working between extensions 1000 and 1001:

1. **Add more extensions** - Create additional SIP users
2. **Configure WebRTC** - Test browser-based calling
3. **Add custom modules** - Integrate your mod_audio_fork and other modules
4. **External calling** - Configure SIP trunks for PSTN calls
5. **Dialplan customization** - Add IVR, call routing, etc.

---

## Support Resources

- **FreeSWITCH Wiki**: https://freeswitch.org/confluence/
- **Docker Hub**: https://hub.docker.com/r/YOUR_DOCKERHUB_USERNAME/freeswitch-base
- **Project Repo**: Your GitHub repository
- **Installation Guide**: See `FREESWITCH_INSTALL.md` for build details

---

**Happy testing!** 🎉📞
