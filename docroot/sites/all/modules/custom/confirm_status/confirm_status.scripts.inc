<?php

/*
 * Tropo doesn't support special UTF chars in text messages.
According to int standards text messages with unicode chars should be limited to 70chars per message.
Messages that are broken into multiple segments, might not arrive in proper sequence.*/
/**
 * Implementation of hook_voipscript_get_script_names()
 */
function confirm_status_voipscript_get_script_names() {
  $script_names[] = 'confirm_status_sms_response_handler_script';
  $script_names[] = 'confirm_status_voice_response_handler_script';
  return $script_names;

}

/**
 *
 */

/**
 * Implementation of hook_voipscript_load_script()
 */
function confirm_status_voipscript_load_script($script_name, $options = NULL) {

  $script = NULL;
  switch ($script_name) {
    default:
      break;

    case 'confirm_status_sms_response_handler_script':
      // Get incoming text message, parameters associated with it and then call
      // next script, which actually processes the response. (For VOIP Drupal
      // internal reasons, these two steps need to be handled as two separate
      // scripts.)

      // VoIP script for Text channel
      $script = new VoipScript('confirm_status_sms_response_handler_script');
      // log the sms
      $log_msg = t("SMS from %caller_number on @date (id: %call_id)",
        array('@date' => format_date(time(), 'custom', 'D, m/d/Y - H:i:s')));
      $script->addLog($log_msg, 'MDT');

      // below was the only way to avoid problems with special characters in the text message
      $options['text'] = '%inbound_text_contents';
      $options['caller_number'] = '%caller_number';
      $options['dest_number'] = '%dest_number';
      $options['call_id'] = '%cid';
      $options['source'] = '%source';
      $options['contact_info'] = '%contact_info';
      $script->addLog('About to call
      confirm_status_sms_process_response_script');
      $script->addGosub('confirm_status_sms_process_response_script', $options);

      break;

    case 'confirm_status_voice_response_handler_script':
      confirm_status_voice_process_response_script();
      // TODO
      // Clear nid from confirm_status_pending table.
    break;

    case 'confirm_status_sms_process_response_script':
      //Helper script to process SMS request
      $script = new VoipScript('confirm_status_sms_process_response_script');
      watchdog('confirm_status', 'In confirm_status_sms_process_response_script');

      // contact_info is passed to VOIP as a json encoded string. Decode to array to simplify handling.
      $contact_info = json_decode($options['contact_info']);
      $options['contact_info'] = $contact_info;

      // Process message and get follow up message to send to user.
      $result_message = _confirm_status_sms_process_response($options);

      $result = voipcall_special_chars($result);

      // Break message into 160 char chunks.
      $text_array = _confirm_status_make_160_char_messages($result);

      // Loop through message(s) and send.
      foreach ($text_array as $index => $text) {
        // Send message.
        watchdog('confirm_status', "Sending message: $text");
        $script->addSendText($text);
        if ($options['source'] == 'twilio') {
          //Only Twilio supports Wait() cmd in text channel
          $script->addWait(2);
        }

        $script->addLog("sending $text");
      }
      $script->addHangup();

      // TODO
      // Clear nid from confirm_status_pending table.
      break;
  }

  return $script;
}

/**
 * This chops the message into 160 character chunks.
 * Split text into an array to be sent one-by-one.
 *
 * @param string $message
 *
 * @return array
 */
function _confirm_status_make_160_char_messages($message) {
  if(strlen($message) == mb_strlen($message)) {
    // send response in chunks of 160 characters
    $char_limit = 160;
  }
  else {
    //This is unicode SMS, can only contain 70 chars
    $char_limit = 70;
  }
  // send response in chunks of $char_limit characters
  if (strlen($message) <= $char_limit) {
    $text_array[] = $message;
  }
  else {
    /// lets use 154 characters and keep room for message number like (1/10),
    /// we can have upto 9 parts of the message (9/9)
    $limit = $char_limit - 6;
    $tmp = wordwrap($message, $limit, '\n');
    $text_array = explode('\n', $tmp);
  }
  $num_of_msg = count($text_array);

  // Number messages (e.g. 1 of 3)
  foreach ($text_array as $index => $text) {
    if ($num_of_msg > 1) {
      $msg_number = ($index + 1);
      $text = "(".$msg_number."/".$num_of_msg.") ".$text;
      $text_array[$index] = $text;
    }
  }

  return $text_array;
}

/**
 * Handle incoming text messages.
 *
 * Check if response is yes or no and if caller number is valid. Update db accordingly.
 *
 * @param array $options
 *   - text, sms message received
 *   - caller_number, person replying to us (sender of SMS)
 *   - dest_number, number where they sent SMS to (our number)
 *   - call_id, identifier for $call object, @see VoipCall
 *   - source, @see VoipCall class
 *   - contact_info, array @see hook_confirm_status
 * 
 * @return $ret
 *  Confirmation message to be sent back to message sender.
 */
function _confirm_status_sms_process_response($options) {
  $msg = $options['text'];
  $caller_number = $options['caller_number'];
  $dest_number = $options['dest_number'];
  $call_id = $options['call_id'];
  $contact_info = $options['contact_info'];
  $callback = $contact_info['callback'];
  $nid = $contact_info['nid'];

  watchdog('confirm_status', "In _confirm_status_sms_process_response with $msg, $caller_number, $dest_number");
  global $language;

  //Get either a default phone number language or default site language.
  // @TODO Figure out language.
  // $default_langcode = !empty($ontrack_phone) ? $ontrack_phone->getLanguage() : $language->language;
  // $langcode = $default_langcode;
  $langcode = 'en';

  $msg = trim($msg);
  if ($msg == '') {
    $ret = t("Empty request.", array(), array('langcode' => $default_langcode)) . ' ' . $help_msg;
    // log the error
    watchdog('confirm_status', "Empty message received. Call ID: ".$call_id);
  }
  else {
    $msg_temp = trim($msg);
    $msg_temp = str_replace(" ", "", $msg_temp);
    if ($msg_tmp == 'yes') {
      // Call callback for "yes".
      // Normal behavior: Leave status unchanged. Person still needs/has the thing the posted
      // about.
      call_user_func($callback, $nid, 'confirmed');
      // Set confirmation message to be sent to user.
      $ret = t('Thank you. Your reply has been processed.', array(), $langcode);
    } 
    else if ($msg_temp == 'no') {
      // Call callback to update status to "closed".
      call_user_func($callback, $nid, 'not confirmed');
      // Set confirmation message to be sent to user.
      $ret = t('Thank you. Your reply has been processed.', array(), $langcode);
    }
    else {
      // Invalid response.   
      call_user_func($callback, $nid, 'invalid');
      // Set confirmation message to be sent to user.
      $ret = t('Sorry. Your reply was invalid and could not be processed.', array(), $langcode);
    }
  }

  return $ret;
}

function confirm_status_voice_process_response_script() {
  // TODO
  // process
  // clear nid from confirm_status_pending table.
}
