# Stereo Channel Assignment - Quick Reference

## TL;DR

**Goal:** Agent always on Channel 0, Customer always on Channel 1

**Solution:** Add `RECORD_STEREO_SWAP=true` for INBOUND calls only

---

## Quick Configuration

### Inbound Calls (Customer → Agent)

```xml
<action application="set" data="RECORD_STEREO_SWAP=true"/>
<action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
```

**Result:** Channel 0 = Agent, Channel 1 = Customer ✓

---

### Outbound Calls (Agent → Customer)

```xml
<!-- No SWAP needed -->
<action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
```

**Result:** Channel 0 = Agent, Channel 1 = Customer ✓

---

## Why This Matters

**WITHOUT consistent assignment:**
- Inbound: Ch0=Customer, Ch1=Agent
- Outbound: Ch0=Agent, Ch1=Customer
- ❌ Analytics broken, inconsistent labeling

**WITH SWAP for inbound:**
- Inbound: Ch0=Agent, Ch1=Customer
- Outbound: Ch0=Agent, Ch1=Customer
- ✅ Consistent for all analytics

---

## The Rule

FreeSWITCH assigns channels relative to the session:
- Channel 0 (Left) = WRITE_STREAM = Local party (session owner)
- Channel 1 (Right) = READ_STREAM = Remote party

**Problem:** Inbound calls attach to customer's session, so customer becomes "local"

**Solution:** Swap channels for inbound to match outbound

---

## Testing

```bash
# Make inbound call, check transcript
# Expected: ch_0 = agent, ch_1 = customer

# Remove SWAP temporarily
# Expected: ch_0 = customer, ch_1 = agent (reversed)
```

---

## Complete Example

```xml
<include>
  <!-- INBOUND with SWAP -->
  <extension name="inbound">
    <condition field="destination_number" expression="^(100[0-9])$">
      <action application="set" data="RECORD_STEREO_SWAP=true"/>
      <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
      <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
      <action application="bridge" data="user/$1"/>
    </condition>
  </extension>

  <!-- OUTBOUND without SWAP -->
  <extension name="outbound">
    <condition field="destination_number" expression="^(1800\d+)$">
      <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
      <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
      <action application="bridge" data="sofia/external/$1@gateway"/>
    </condition>
  </extension>
</include>
```

---

For detailed explanation, see [STEREO_CHANNEL_ASSIGNMENT.md](./STEREO_CHANNEL_ASSIGNMENT.md)
