<?php

/*
 * Tropo doesn't support special UTF chars in text messages.
According to int standards text messages with unicode chars should be limited to 70chars per message.
Messages that are broken into multiple segments, might not arrive in proper sequence.*/
/**
 * Implementation of hook_voipscript_get_script_names()
 */
function confirm_status_voipscript_get_script_names() {
  $script_names[] = 'confirm_status_sms_handler_script';
  return $script_names;

}

/**
 * Implementation of hook_voipscript_load_script()
 */
function confirm_status_voipscript_load_script($script_name, $options = NULL) {

  $script = NULL;
  switch ($script_name) {
    default:
      break;

    case 'confirm_status_sms_handler_script':
      // Get incoming text message, parameters associated with it and then call
      // next script, which actually processes the response. (For VOIP Drupal
      // internal reasons, these two steps need to be handled as two separate
      // scripts.)

      // VoIP script for Text channel
      $script = new VoipScript('confirm_status_sms_handler_script');
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
      $script->addGosub('confirm_status_sms_process_response_script', $options);
      break;

    case 'confirm_status_sms_process_response_script':
      //Helper script to process SMS request
      $script = new VoipScript('confirm_status_sms_process_response_script');

// @TODO call this:
_confirm_status_sms_process_response();

      $result = voipcall_special_chars($result);

      // Break message into 160 char chunks.
      $text_array = _confirm_status_make_160_char_messages($result);

      // Loop through message(s) and send.
      foreach ($text_array as $index => $text) {
        // Send message.
        $script->addSendText($text);
        if ($options['source'] == 'twilio') {
          //Only Twilio supports Wait() cmd in text channel
          $script->addWait(2);
        }

        $script->addLog("sending $text");
      }
      $script->addHangup();
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
 * @param string $msg
 *  Text message received.
 *
 * @param string $dest_number
 *  Destination number from incoming message (this is OUR number).
 *
 * @param $call_id
 *  ID of the VOIP call object
 *
 * @return $ret
 *  Confirmation message to be sent back to message sender.
 */
function _confirm_status_sms_process_response($msg, $caller_number, $dest_number, $call_id) {
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
      // Leave status unchanged. Person still needs/has the thing the posted
      // about.

    } 
    else {
      
    }

    // CONTINUE HERE

        //Else this is CREATE request
        //Get array of valid site_ids for this SMS number
        $valid_site_ids = $ontrack_phone->getSiteIds();
        if (count($valid_site_ids) == 1) {
          //There is just one matching site for this SMS number, check
          //if its using retrieve SMS syntax
          $site_id = reset($valid_site_ids);
          $ontrack_site = new OnTrackSite($site_id);
          if ($ontrack_phone->getSyntax() == 'retrieve') {
            //This SMS number is set to retrieve id from dest_number so create ticket
            $ticket_text = $msg;
            $site_langcode = $ontrack_site->getSiteLanguage();
            //Use site language, if not set use phone number languaage
            $langcode = !empty($site_langcode) ? $site_langcode : $default_langcode;
            $ret = ontrackvoip_create_ticket($ontrack_site, $ticket_text, $caller_number, $langcode);
            return $ret;
          }
        }
        #Else
        //Check if this SMS has embedded site id in its body
        $request = explode(" ", $msg);
        $site_id = $request[0];
        if (is_numeric($site_id)) {
          //SMS syntax: embedded site id
          //Check if site_id is valid
          if (isset($valid_site_ids[$site_id])) {
            //Load site object for further info like language, site name,...
            $ontrack_site = new OnTrackSite($site_id);
            //Create new ticket
            unset($request[0]); //remove site_id from ticket_text
            $site_langcode = $ontrack_site->getSiteLanguage();
            //Use site language, if not set use phone number languaage
            $langcode = !empty($site_langcode) ? $site_langcode : $default_langcode;
            $ticket_text = implode(" ", $request);
            $ret = ontrackvoip_create_ticket($ontrack_site, $ticket_text, $caller_number, $langcode);
          }
          else {
            //Invalid site id
            //@todo: replace XXX with contact number
            $ret = t("!site_id is not a valid site identifier. Please contact the site administrator for additional information.",
              array(
                '!site_id' => $site_id,
              ),
              array(
                'langcode' => $default_langcode,
              )
            );

            //log the error
            ontrackvoip_log($site_id . " is not a valid site. Call ID: ".$call_id);
          }
        }
        else {
          //Missing site id
          if(!$ret) {
            $ret = t("Missing site id.", array(), array('langcode' => $default_langcode)) . ' ' . $help_msg;
            //log the error
            ontrackvoip_log("Message with invalid request received. Message content: ".print_r($msg, true)." Call ID: ".$call_id);
          }
        }
      }
    }
  }
  return $ret;
}

