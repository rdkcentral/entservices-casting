#** *****************************************************************************
# *
# * If not stated otherwise in this file or this component's LICENSE file the
# * following copyright and licenses apply:
# *
# * Copyright 2024 RDK Management
# *
# * Licensed under the Apache License, Version 2.0 (the "License");
# * you may not use this file except in compliance with the License.
# * You may obtain a copy of the License at
# *
# *
# http://www.apache.org/licenses/LICENSE-2.0
# *
# * Unless required by applicable law or agreed to in writing, software
# * distributed under the License is distributed on an "AS IS" BASIS,
# * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# * See the License for the specific language governing permissions and
# * limitations under the License.
# *
#* ******************************************************************************

# Testcase ID : TCID008
# Testcase Description : Hit the curl command for getActiveSourceStatus method and
# verify that status is obtained as false in output response

from Utilities import Utils, ReportGenerator
from HdmiCecSource import HdmiCecSourceApis

# store the expected output response
expected_output_response = '{"jsonrpc":"2.0","id":42,"result":{"status":false,"success":true}}'

print("TC Description - Hit the curl command for getActiveSourceStatus method and verify that status is obtained as false in output response")

print("---------------------------------------------------------------------------------------------------------------------------")
# send the curl command and fetch the output json response
Utils.initiliaze_flask_for_HdmiCecSource()
curl_response = Utils.send_curl_command(HdmiCecSourceApis.get_active_source_status)
if curl_response:
    Utils.info_log("curl command send for get_active_source")
else:
    Utils.error_log("curl command send failed for get_active_source")

print("---------------------------------------------------------------------------------------------------------------------------")
# compare both expected and received output responses
if str(curl_response) == str(expected_output_response):
    status = 'Pass'
    message = 'Output response is matching with expected one'
else:
    status = 'Fail'
    message = 'Output response is different from expected one'

# generate logs in terminal
tc_id = 'TCID008_getActiveSourceStatus_false'
print("Testcase ID : " + tc_id)
print("Testcase Output Response : " + curl_response)
print("Testcase Status : " + status)
print("Testcase Message : " + message)
print("")

if status == 'Pass':
    ReportGenerator.passed_tc_list.append(tc_id)
else:
    ReportGenerator.failed_tc_list.append(tc_id)
Utils.initiliaze_flask_for_HdmiCecSource()
# push the testcase execution details to report file
ReportGenerator.append_test_results_to_csv(tc_id, curl_response, status, message)
