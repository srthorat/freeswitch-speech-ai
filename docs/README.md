# Documentation

This directory contains technical documentation and guides for the freeswitch-speech-ai project.

## Available Documents

### 📚 Guides

- **[Stereo Channel Assignment Guide](./STEREO_CHANNEL_ASSIGNMENT.md)** - Comprehensive guide on FreeSWITCH stereo channel assignment, including:
  - How FreeSWITCH assigns audio streams to channels
  - Inbound vs outbound call scenarios
  - Solutions for consistent channel assignment
  - Complete dialplan examples
  - Testing and troubleshooting

- **[Stereo Channels Quick Reference](./QUICK_REFERENCE_STEREO_CHANNELS.md)** - TL;DR version with quick configuration snippets

- **[Real-Time Transcription Delivery](./REALTIME_TRANSCRIPTION_DELIVERY.md)** - Complete guide for delivering transcription events to frontend apps, including:
  - Managed services comparison (Pusher, Ably, PubNub)
  - Self-hosted solutions (Socket.io + Redis, WebSocket, SSE)
  - Lua vs C++ implementation analysis
  - Performance benchmarks and cost analysis
  - Production architecture and deployment
  - Complete code examples

- **[XML Dialplan vs Lua Scripting](./DIALPLAN_VS_LUA.md)** - Comprehensive comparison of FreeSWITCH call control approaches, including:
  - Performance analysis (speed, memory, throughput)
  - Feature comparison (variables, conditions, loops, functions)
  - When to use each approach
  - Code examples for common tasks
  - Hybrid approaches and best practices
  - Migration guide from XML to Lua

## Module-Specific Documentation

Each transcription module has its own detailed README:

- [mod_aws_transcribe](../modules/mod_aws_transcribe/README.md) - AWS Transcribe Streaming API integration
- [mod_deepgram_transcribe](../modules/mod_deepgram_transcribe/README.md) - Deepgram Nova-2 API integration
- [mod_azure_transcribe](../modules/mod_azure_transcribe/README.md) - Azure Speech Services integration
- [mod_google_transcribe](../modules/mod_google_transcribe/README.md) - Google Cloud Speech-to-Text integration
- [mod_audio_fork](../modules/mod_audio_fork/README.md) - Generic audio streaming over WebSockets

## Configuration Examples

- [Per-User Multi-Service Configuration](../examples/freeswitch-config/PER_USER_MULTI_SERVICE.md) - Enable different services per user
- [Directory Configuration](../examples/freeswitch-config/directory/README.md) - User directory setup
- [Docker Deployment](../dockerfiles/README.md) - Docker build and deployment guide

## Quick Start

1. **Installation:** See [INSTALL.md](../INSTALL.md) for build instructions
2. **Docker Setup:** See [dockerfiles/README.md](../dockerfiles/README.md) for Docker deployment
3. **Stereo Setup:** Start with [Quick Reference](./QUICK_REFERENCE_STEREO_CHANNELS.md) for channel assignment

## Contributing

When adding new documentation:
1. Create markdown files in this directory
2. Update this README with links
3. Use clear headings and code examples
4. Include practical examples and troubleshooting sections

## Getting Help

- Check module-specific READMEs first
- Review troubleshooting sections in guides
- Open an issue on GitHub with:
  - FreeSWITCH version
  - Module version
  - Relevant logs
  - Configuration snippets
