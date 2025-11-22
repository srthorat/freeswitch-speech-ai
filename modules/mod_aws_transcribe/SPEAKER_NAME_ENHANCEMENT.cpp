/**
 * AWS Transcribe Module - Speaker Name Enhancement
 *
 * This file shows the EXACT code changes needed to add automatic
 * speaker name injection into transcription JSON output.
 *
 * File to modify: modules/mod_aws_transcribe/aws_transcribe_glue.cpp
 * Function: GStreamer::processData() - around line 258-300
 */

// ============================================================================
// STEP 1: Read speaker names from channel variables (add after line 272)
// ============================================================================

if (m_transcript.TranscriptHasBeenSet()) {
    switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
    if (psession) {

        // ✅ ADD THIS BLOCK HERE (after "if (psession) {")
        // --------------------------------------------------
        // Get speaker names from channel variables
        switch_channel_t* channel = switch_core_session_get_channel(psession);
        const char* caller_name = switch_channel_get_variable(channel, "caller_name");
        const char* caller_number = switch_channel_get_variable(channel, "caller_number");
        const char* callee_name = switch_channel_get_variable(channel, "callee_name");
        const char* callee_number = switch_channel_get_variable(channel, "callee_number");

        // Set defaults if not available
        if (!caller_name) caller_name = "";
        if (!caller_number) caller_number = "";
        if (!callee_name) callee_name = "";
        if (!callee_number) callee_number = "";
        // --------------------------------------------------


        bool isFinal = false;
        std::ostringstream s;
        s << "[";
        for (auto&& r : m_transcript.GetTranscript().GetResults()) {
            int count = 0;
            std::ostringstream t1;


            // ✅ ADD THIS BLOCK HERE (after "std::ostringstream t1;")
            // --------------------------------------------------
            // Extract channel_id from AWS result
            std::string channelId = "";
            if (r.ChannelIdHasBeenSet()) {
                channelId = r.GetChannelId();
            }

            // Map channel_id to speaker name
            std::string speakerName = "";
            std::string speakerNumber = "";
            if (!channelId.empty()) {
                if (channelId == "ch_0") {
                    speakerName = caller_name;
                    speakerNumber = caller_number;
                } else if (channelId == "ch_1") {
                    speakerName = callee_name;
                    speakerNumber = callee_number;
                }
            }
            // --------------------------------------------------


            if (!isFinal && !r.GetIsPartial()) isFinal = true;


            // ✅ REPLACE THIS LINE:
            // t1 << "{\"is_final\": " << (r.GetIsPartial() ? "false" : "true") << ", \"alternatives\": [";

            // WITH THIS:
            // --------------------------------------------------
            t1 << "{\"is_final\": " << (r.GetIsPartial() ? "false" : "true");

            // Add channel_id if available
            if (!channelId.empty()) {
                t1 << ", \"channel_id\": \"" << channelId << "\"";
            }

            // Add speaker name and number if mapped
            if (!speakerName.empty()) {
                t1 << ", \"speaker_name\": \"" << speakerName << "\"";
            }
            if (!speakerNumber.empty()) {
                t1 << ", \"speaker_number\": \"" << speakerNumber << "\"";
            }

            t1 << ", \"alternatives\": [";
            // --------------------------------------------------


            for (auto&& alt : r.GetAlternatives()) {
                std::ostringstream t2;
                if (count++ == 0) t2 << "{\"transcript\": \"" << alt.GetTranscript() << "\"}";
                else t2 << ", {\"transcript\": \"" << alt.GetTranscript() << "\"}";
                t1 << t2.str();
            }
            t1 << "]}";
            s << t1.str();
        }
        s << "]";
        if (0 != s.str().compare("[]") && (isFinal || m_interim)) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer::writing transcript %p: %s\n", this, s.str().c_str() );
            m_responseHandler(psession, s.str().c_str(), m_bugname.c_str());
        }
        TranscriptEvent empty;
        m_transcript = empty;

        switch_core_session_rwunlock(psession);
    }
}


// ============================================================================
// RESULT: JSON OUTPUT WILL NOW INCLUDE SPEAKER NAMES
// ============================================================================

/**
 * BEFORE (without changes):
 * [
 *   {
 *     "is_final": true,
 *     "alternatives": [{
 *       "transcript": "Hello, how can I help you?"
 *     }]
 *   }
 * ]
 *
 * AFTER (with changes):
 * [
 *   {
 *     "is_final": true,
 *     "channel_id": "ch_0",
 *     "speaker_name": "John Doe",
 *     "speaker_number": "1002",
 *     "alternatives": [{
 *       "transcript": "Hello, how can I help you?"
 *     }]
 *   }
 * ]
 */


// ============================================================================
// HOW TO APPLY THESE CHANGES
// ============================================================================

/**
 * 1. Open the file:
 *    vi modules/mod_aws_transcribe/aws_transcribe_glue.cpp
 *
 * 2. Find the processData() function (around line 258)
 *
 * 3. Locate: if (m_transcript.TranscriptHasBeenSet()) {
 *
 * 4. Add the speaker name reading code right after line 272
 *
 * 5. Add the channel mapping code in the for loop
 *
 * 6. Modify the JSON building line to include speaker fields
 *
 * 7. Save and rebuild:
 *    make mod_aws_transcribe-install
 *    fs_cli -x 'reload mod_aws_transcribe'
 */


// ============================================================================
// DEPENDENCIES
// ============================================================================

/**
 * Make sure your dialplan exports these variables:
 *
 * <action application="export" data="nolocal:caller_name=${effective_caller_id_name}"/>
 * <action application="export" data="nolocal:caller_number=${caller_id_number}"/>
 * <action application="export" data="nolocal:callee_name=${callee_id_name}"/>
 * <action application="export" data="nolocal:callee_number=${destination_number}"/>
 */
