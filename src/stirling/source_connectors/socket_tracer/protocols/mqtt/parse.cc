/*
 * Copyright 2018- The Pixie Authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <memory>
#include <string_view>
#include <iomanip> 

#include "src/common/base/base.h"
#include "src/stirling/utils/parse_state.h"
#include "src/stirling/utils/binary_decoder.h"
#include "src/stirling/source_connectors/socket_tracer/protocols/mqtt/types.h"

#define PX_ASSIGN_OR_RETURN_ERROR(expr, val_or) \
  PX_ASSIGN_OR(expr, val_or, return ParseState::kNeedsMoreData)

namespace px {
namespace stirling {
namespace protocols {

namespace mqtt {

enum class MqttControlPacketType : uint8_t {
    CONNECT = 1,
    CONNACK = 2,
    PUBLISH = 3,
    PUBACK = 4,
    PUBREC = 5,
    PUBREL = 6,
    PUBCOMP = 7,
    SUBSCRIBE = 8,
    SUBACK = 9,
    UNSUBSCRIBE = 10,
    UNSUBACK = 11,
    PINGREQ = 12,
    PINGRESP = 13,
    DISCONNECT = 14,
    INVALID = 0xff,
};

std::unordered_map<MqttControlPacketType, std::string> ControlPacketTypeStrings = {
        {MqttControlPacketType::CONNECT, "CONNECT"},
        {MqttControlPacketType::CONNACK, "CONNACK"},
        {MqttControlPacketType::PUBLISH, "PUBLISH"},
        {MqttControlPacketType::PUBACK, "PUBACK"},
        {MqttControlPacketType::PUBREC, "PUBREC"},
        {MqttControlPacketType::PUBREL, "PUBREL"},
        {MqttControlPacketType::PUBCOMP, "PUBCOMP"},
        {MqttControlPacketType::SUBSCRIBE, "SUBSCRIBE"},
        {MqttControlPacketType::SUBACK, "SUBACK"},
        {MqttControlPacketType::UNSUBSCRIBE, "UNSUBSCRIBE"},
        {MqttControlPacketType::UNSUBACK, "UNSUBACK"},
        {MqttControlPacketType::PINGREQ, "PINGREQ"},
        {MqttControlPacketType::PINGRESP, "PINGRESP"},
        {MqttControlPacketType::DISCONNECT, "DISCONNECT"},
        {MqttControlPacketType::INVALID, "INVALID"},
};

enum class PropertyCode: uint8_t {
    PayloadFormatIndicator = 0x01,
    MessageExpiryInterval = 0x02,
    ContentType = 0x03,
    ResponseTopic = 0x08,
    CorrelationData = 0x09,
    SubscriptionIdentifier = 0x0B,
    SessionExpiryInterval = 0x11,
    AssignedClientIdentifier = 0x12,
    ServerKeepAlive = 0x13,
    AuthenticationMethod = 0x15,
    AuthenticationData = 0x16,
    RequestProblemInformation = 0x17,
    WillDelayInterval = 0x18,
    RequestResponseInformation = 0x19,
    ResponseInformation = 0x1A,
    ServerReference = 0x1C,
    ReasonString = 0x1F,
    ReceiveMaximum = 0x21,
    TopicAliasMaximum = 0x22,
    TopicAlias = 0x23,
    MaximumQos = 0x24,
    RetainAvailable = 0x25,
    UserProperty = 0x26,
    MaximumPacketSize = 0x27,
    WildcardSubscriptionAvailable = 0x28,
    SubscriptionIdentifiersAvailable = 0x29,
    SharedSubscriptionAvailable = 0x2A,
    Invalid = 0xFF
};

static inline MqttControlPacketType GetControlPacketType(uint8_t control_packet_type_code) {
    if (control_packet_type_code == static_cast<uint8_t>(MqttControlPacketType::PUBLISH)) {
        return MqttControlPacketType::PUBLISH;
    }

    switch (control_packet_type_code) {
        case static_cast<uint8_t>(MqttControlPacketType::CONNECT):
            return MqttControlPacketType::CONNECT;
        case static_cast<uint8_t>(MqttControlPacketType::CONNACK):
            return MqttControlPacketType::CONNACK;
        case static_cast<uint8_t>(MqttControlPacketType::PUBACK):
            return MqttControlPacketType::PUBACK;
        case static_cast<uint8_t>(MqttControlPacketType::PUBREC):
            return MqttControlPacketType::PUBREC;
        case static_cast<uint8_t>(MqttControlPacketType::PUBREL):
            return MqttControlPacketType::PUBREL;
        case static_cast<uint8_t>(MqttControlPacketType::PUBCOMP):
            return MqttControlPacketType::PUBCOMP;
        case static_cast<uint8_t>(MqttControlPacketType::SUBSCRIBE):
            return MqttControlPacketType::SUBSCRIBE;
        case static_cast<uint8_t>(MqttControlPacketType::SUBACK):
            return MqttControlPacketType::SUBACK;
        case static_cast<uint8_t>(MqttControlPacketType::UNSUBSCRIBE):
            return MqttControlPacketType::UNSUBSCRIBE;
        case static_cast<uint8_t>(MqttControlPacketType::UNSUBACK):
            return MqttControlPacketType::UNSUBACK;
        case static_cast<uint8_t>(MqttControlPacketType::PINGREQ):
            return MqttControlPacketType::PINGREQ;
        case static_cast<uint8_t>(MqttControlPacketType::PINGRESP):
            return MqttControlPacketType::PINGRESP;
        case static_cast<uint8_t>(MqttControlPacketType::DISCONNECT):
            return MqttControlPacketType::DISCONNECT;
        default:
            return MqttControlPacketType::INVALID;
    }
}

static inline std::tuple<unsigned long, size_t> VariableLengthEncodingDecoder(BinaryDecoder* decoder) {
    unsigned long multiplier = 1;
    unsigned long decoded_value = 0;
    size_t num_bytes = 0;
    uint8_t encoded_byte;
    do {
        // encoded byte cannot be 0 as there is minimum variable header size greater than 0
        encoded_byte = decoder->ExtractBEInt<uint8_t>().ValueOrDie();
        decoded_value += (encoded_byte & 127) * multiplier;
        // size of the remaining length cannot be above 4 bytes
        if (multiplier > (size_t)128 * 128 * 128) {
            return std::make_tuple(0, num_bytes);
        }
        multiplier *= 128;
        num_bytes += 1;
    } while ((encoded_byte & 128) != 0);

    return std::make_tuple(decoded_value, num_bytes);
}

ParseState ParseProperties(Message* result, BinaryDecoder* decoder, size_t& properties_length) {
    uint8_t property_code;
    while (properties_length > 0) {
        // Extracting the property code
        PX_ASSIGN_OR_RETURN_ERROR(property_code, decoder->ExtractBEInt<uint8_t>());
        properties_length -= 1;

        switch (property_code) {
            case static_cast<uint8_t>(PropertyCode::PayloadFormatIndicator): {
                PX_ASSIGN_OR_RETURN_ERROR(uint8_t payload_format_indicator, decoder->ExtractBEInt<uint8_t>());
                if (payload_format_indicator == 0x00) {
                    result->properties["payload_format"] = "unspecified";
                } else if (payload_format_indicator == 0x01) {
                    result->properties["payload_format"] = "utf-8";
                } else {
                    return ParseState::kInvalid;
                }
                properties_length -= 1;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::MessageExpiryInterval): {
                PX_ASSIGN_OR_RETURN_ERROR(uint32_t message_expiry_interval, decoder->ExtractBEInt<uint32_t>());
                result->properties["message_expiry_interval"] = std::to_string(message_expiry_interval);
                properties_length -= 4;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::ContentType): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t property_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view content_type, decoder->ExtractString(property_length));
                result->properties["content_type"] = std::string(content_type);
                properties_length -= property_length;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::ResponseTopic): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t property_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view response_topic, decoder->ExtractString(properties_length));
                result->properties["response_topic"] = std::string(response_topic);
                properties_length -= property_length;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::CorrelationData): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t property_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view correlation_data, decoder->ExtractString((size_t)property_length));
                result->properties["correlation_data"] = std::string(correlation_data);
                properties_length -= property_length;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::SubscriptionIdentifier): {
                unsigned long subscription_id;
                size_t num_bytes;
                std::tie(subscription_id, num_bytes) = VariableLengthEncodingDecoder(decoder);
                result->properties["subscription_id"] = std::to_string(subscription_id);
                properties_length -= num_bytes;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::SessionExpiryInterval): {
                PX_ASSIGN_OR_RETURN_ERROR(uint32_t session_expiry_interval, decoder->ExtractBEInt<uint32_t>());
                result->properties["session_expiry_interval"] = std::to_string(session_expiry_interval);
                properties_length -= 4;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::AssignedClientIdentifier): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t property_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view assigned_client_identifier, decoder->ExtractString(property_length));
                result->properties["assigned_client_identifier"] = std::string(assigned_client_identifier);
                properties_length -= property_length;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::ServerKeepAlive): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t server_keep_alive, decoder->ExtractBEInt<uint16_t>());
                result->properties["server_keep_alive"] = std::to_string(server_keep_alive);
                properties_length -= 2;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::AuthenticationMethod): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t property_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view auth_method, decoder->ExtractString(property_length));
                result->properties["auth_method"] = std::string(auth_method);
                properties_length -= property_length;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::AuthenticationData): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t property_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view auth_data, decoder->ExtractString((size_t)property_length));
                result->properties["auth_data"] = std::string(auth_data);
                properties_length -= property_length;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::RequestProblemInformation): {
                PX_ASSIGN_OR_RETURN_ERROR(uint8_t request_problem_information, decoder->ExtractBEInt<uint8_t>());
                result->properties["request_problem_information"] = std::to_string(request_problem_information);
                properties_length -= 1;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::WillDelayInterval): {
                PX_ASSIGN_OR_RETURN_ERROR(uint32_t will_delay_interval, decoder->ExtractBEInt<uint32_t>());
                result->properties["will_delay_interval"] = std::to_string(will_delay_interval);
                properties_length -= 4;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::RequestResponseInformation): {
                PX_ASSIGN_OR_RETURN_ERROR(uint8_t request_response_information, decoder->ExtractBEInt<uint8_t>());
                result->properties["request_response_information"] = std::to_string(request_response_information);
                properties_length -= 1;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::ResponseInformation): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t property_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view response_information, decoder->ExtractString(properties_length));
                result->properties["response_information"] = std::string(response_information);
                properties_length -= property_length;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::ServerReference): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t property_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view server_reference, decoder->ExtractString(property_length));
                result->properties["server_reference"] = std::string(server_reference);
                properties_length -= property_length;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::ReasonString): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t property_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view reason_string, decoder->ExtractString(property_length));
                result->properties["reason_string"] = std::string(reason_string);
                properties_length -= property_length;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::ReceiveMaximum): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t receive_maximum, decoder->ExtractBEInt<uint16_t>());
                result->properties["receive_maximum"] = std::to_string(receive_maximum);
                properties_length -= 2;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::TopicAliasMaximum): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t topic_alias_maximum, decoder->ExtractBEInt<uint16_t>());
                result->properties["topic_alias_maximum"] = std::to_string(topic_alias_maximum);
                properties_length -= 2;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::TopicAlias): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t topic_alias, decoder->ExtractBEInt<uint16_t>());
                result->properties["topic_alias"] = std::to_string(topic_alias);
                properties_length -= 2;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::MaximumQos): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t topic_alias, decoder->ExtractBEInt<uint16_t>());
                result->properties["topic_alias"] = std::to_string(topic_alias);
                properties_length -= 2;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::RetainAvailable): {
                PX_ASSIGN_OR_RETURN_ERROR(uint8_t retain_available, decoder->ExtractBEInt<uint8_t>());
                result->properties["retain_available"] = std::to_string(retain_available);
                properties_length -= 1;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::UserProperty): {
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t key_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view key, decoder->ExtractString((size_t)key_length));
                properties_length -= key_length;
                PX_ASSIGN_OR_RETURN_ERROR(uint16_t value_length, decoder->ExtractBEInt<uint16_t>());
                properties_length -= 2;
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view value, decoder->ExtractString((size_t)value_length));
                properties_length -= value_length;
                // For multiple user properties present, append to string if user property already present
                if (result->properties.find("user-properties") == result->properties.end()) {
                    result->properties["user-properties"] = "{" + std::string(key) + ":" + std::string(value) + "}";
                } else {
                    result->properties["user-properties"] += ", {" + std::string(key) + ":" + std::string(value) + "}";
                }
                break;
            }
            case static_cast<uint8_t>(PropertyCode::MaximumPacketSize): {
                PX_ASSIGN_OR_RETURN_ERROR(uint32_t maximum_packet_size, decoder->ExtractBEInt<uint32_t>());
                result->properties["maximum_packet_size"] = std::to_string(maximum_packet_size);
                properties_length -= 4;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::WildcardSubscriptionAvailable): {
                PX_ASSIGN_OR_RETURN_ERROR(uint8_t wildcard_subscription_available, decoder->ExtractBEInt<uint8_t>());
                result->properties["retain_available"] = (wildcard_subscription_available == 1)?"true":"false";
                properties_length -= 1;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::SubscriptionIdentifiersAvailable): {
                PX_ASSIGN_OR_RETURN_ERROR(uint8_t subscription_id_available, decoder->ExtractBEInt<uint8_t>());
                result->properties["subscription_id_available"] = (subscription_id_available == 1)?"true":"false";
                properties_length -= 1;
                break;
            }
            case static_cast<uint8_t>(PropertyCode::SharedSubscriptionAvailable): {
                PX_ASSIGN_OR_RETURN_ERROR(uint8_t shared_subscription_available, decoder->ExtractBEInt<uint8_t>());
                result->properties["subscription_id_available"] = (shared_subscription_available == 1)?"true":"false";
                properties_length -= 1;
                break;
            }
            default:
                return ParseState::kInvalid;
        }
    }
    return ParseState::kSuccess;
}

ParseState ParseVariableHeader(Message* result, BinaryDecoder* decoder, MqttControlPacketType& control_packet_type) {
    switch (control_packet_type) {
        case MqttControlPacketType::CONNECT: {
            PX_ASSIGN_OR_RETURN_ERROR(uint8_t protocol_name_length, decoder->ExtractBEInt<uint16_t>());
            PX_ASSIGN_OR_RETURN_ERROR(std::string_view protocol_name,decoder->ExtractString((size_t)protocol_name_length));
            CTX_DCHECK(protocol_name == "MQTT");
            PX_ASSIGN_OR_RETURN_ERROR(uint8_t protocol_version, decoder->ExtractBEInt<uint8_t>());
            CTX_DCHECK(protocol_version == 5);

            PX_ASSIGN_OR_RETURN_ERROR(uint8_t connect_flags, decoder->ExtractBEInt<uint8_t>());
            result->header_fields["username_flag"] = connect_flags >> 7;
            result->header_fields["password_flag"] = (connect_flags >> 6) & 0x1;
            result->header_fields["will_retain"] = (connect_flags >> 5) & 0x1;
            result->header_fields["will_qos"] = (connect_flags >> 3) & 0x3;
            result->header_fields["will_flag"] = (connect_flags >> 2) & 0x1;
            result->header_fields["clean_start"] = (connect_flags >> 1) & 0x1;

            PX_ASSIGN_OR_RETURN_ERROR(result->header_fields["keep_alive"], decoder->ExtractBEInt<uint16_t>());

            size_t properties_length;
            std::tie(properties_length, std::ignore) = VariableLengthEncodingDecoder(decoder);
            return ParseProperties(result, decoder, properties_length);
        }
        case MqttControlPacketType::CONNACK: {
            PX_ASSIGN_OR_RETURN_ERROR(uint8_t connack_flags, decoder->ExtractBEInt<uint8_t>());
            PX_ASSIGN_OR_RETURN_ERROR(result->header_fields["reason_code"], decoder->ExtractBEInt<uint8_t>());

            result->header_fields["session_present"] = connack_flags;

            size_t properties_length;
            std::tie(properties_length, std::ignore) = VariableLengthEncodingDecoder(decoder);
            return ParseProperties(result, decoder, properties_length);
        }
        case MqttControlPacketType::PUBLISH: {
            PX_ASSIGN_OR_RETURN_ERROR(uint16_t topic_length, decoder->ExtractBEInt<uint16_t>());
            PX_ASSIGN_OR_RETURN_ERROR(std::string_view topic_name, decoder->ExtractString(topic_length));
            result->payload["topic_name"] = std::string(topic_name);

            // Storing variable header length for use in payload length calculation
            result->header_fields["variable_header_length"] = 2 + (uint32_t)topic_length;

            // Check if packet qos is not 0, only then load packet id
            if(result->header_fields.find("qos") == result->header_fields.end()) {
                return ParseState::kInvalid;
            }
            if (result->header_fields["qos"] != 0) {
                PX_ASSIGN_OR_RETURN_ERROR(result->header_fields["packet_identifier"], decoder->ExtractBEInt<uint16_t>());
                result->header_fields["variable_header_length"] += 2;
            }
            size_t properties_length, num_bytes;
            std::tie(properties_length, num_bytes) = VariableLengthEncodingDecoder(decoder);
            result->header_fields["variable_header_length"] += (uint32_t)(num_bytes + properties_length);

            return ParseProperties(result, decoder, properties_length);
        }
        case MqttControlPacketType::PUBACK:
        case MqttControlPacketType::PUBREC:
        case MqttControlPacketType::PUBREL:
        case MqttControlPacketType::PUBCOMP: {
            PX_ASSIGN_OR_RETURN_ERROR(result->header_fields["packet_identifier"], decoder->ExtractBEInt<uint16_t>());
            if(result->header_fields.find("remaining_length") == result->header_fields.end()) {
                return ParseState::kInvalid;
            }
            if (result->header_fields["remaining_length"] >= 3) {
                PX_ASSIGN_OR_RETURN_ERROR(result->header_fields["reason_code"], decoder->ExtractBEInt<uint8_t>());
            }

            if (result->header_fields["remaining_length"] >= 4) {
                size_t properties_length;
                std::tie(properties_length, std::ignore) = VariableLengthEncodingDecoder(decoder);
                return ParseProperties(result, decoder, properties_length);
            }

            return ParseState::kSuccess;
        }
        case MqttControlPacketType::SUBSCRIBE:
        case MqttControlPacketType::SUBACK:
        case MqttControlPacketType::UNSUBSCRIBE:
        case MqttControlPacketType::UNSUBACK: {
            PX_ASSIGN_OR_RETURN_ERROR(result->header_fields["packet_identifier"], decoder->ExtractBEInt<uint16_t>());
            // Storing variable header length for use in payload length calculation
            result->header_fields["variable_header_length"] = 2;
            size_t properties_length, num_bytes;
            std::tie(properties_length, num_bytes) = VariableLengthEncodingDecoder(decoder);
            result->header_fields["variable_header_length"] += num_bytes + properties_length;
            return ParseProperties(result, decoder, properties_length);
        }
        case MqttControlPacketType::DISCONNECT: {
            PX_ASSIGN_OR_RETURN_ERROR(result->header_fields["reason_code"], decoder->ExtractBEInt<uint8_t>());

            if (result->header_fields["remaining_length"] > 1) {
                size_t properties_length;
                std::tie(properties_length, std::ignore) = VariableLengthEncodingDecoder(decoder);
                return ParseProperties(result, decoder, properties_length);
            }
            return ParseState::kSuccess;
        }
        default:
            return ParseState::kSuccess;
    }
}

ParseState ParsePayload(Message* result, BinaryDecoder* decoder, MqttControlPacketType& control_packet_type) {
    switch (control_packet_type) {
        case MqttControlPacketType::CONNECT: {
            PX_ASSIGN_OR_RETURN_ERROR(uint16_t client_id_length, decoder->ExtractBEInt<uint16_t>());
            PX_ASSIGN_OR_RETURN_ERROR(std::string_view client_id, decoder->ExtractString(client_id_length));
            result->payload["client_id"] =  std::string(client_id);

            if (result->header_fields["will_flag"]) {
                size_t will_properties_length, will_topic_length, will_payload_length;

                std::tie(will_properties_length, std::ignore) = VariableLengthEncodingDecoder(decoder);
                if (ParseProperties(result, decoder, will_properties_length) == ParseState::kInvalid) {
                    return ParseState::kInvalid;
                }

                PX_ASSIGN_OR_RETURN_ERROR(will_topic_length, decoder->ExtractBEInt<uint16_t>());
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view will_topic, decoder->ExtractString(will_topic_length));
                result->payload["will_topic"] = std::string(will_topic);

                PX_ASSIGN_OR_RETURN_ERROR(will_payload_length, decoder->ExtractBEInt<uint16_t>());
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view will_payload, decoder->ExtractString(will_payload_length));
                result->payload["will_payload"] = std::string(will_payload);
            }

            if (result->header_fields["username_flag"]) {
                PX_ASSIGN_OR_RETURN_ERROR(size_t username_length, decoder->ExtractBEInt<uint16_t>());
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view username, decoder->ExtractString(username_length));
                result->payload["username"] = std::string(username);
            }

            if (result->header_fields["password_flag"]) {
                PX_ASSIGN_OR_RETURN_ERROR(size_t password_length, decoder->ExtractBEInt<uint16_t>());
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view password, decoder->ExtractString(password_length));
                result->payload["password"] = std::string(password);
            }

            return ParseState::kSuccess;
        }
        case MqttControlPacketType::CONNACK:
            return ParseState::kSuccess;
        case MqttControlPacketType::PUBLISH: {
            if ((result->header_fields.find("remaining_length") == result->header_fields.end()) ||
                (result->header_fields.find("variable_header_length") == result->header_fields.end())) {
                    return ParseState::kInvalid;
            }
            size_t payload_length = (size_t)(result->header_fields["remaining_length"] - result->header_fields["variable_header_length"]);
            PX_ASSIGN_OR_RETURN_ERROR(std::string_view payload, decoder->ExtractString(payload_length));
            result->payload["publish_message"] = std::string(payload);
            return ParseState::kSuccess;
        }
        case MqttControlPacketType::PUBACK:
        case MqttControlPacketType::PUBREC:
        case MqttControlPacketType::PUBREL:
        case MqttControlPacketType::PUBCOMP:
            return ParseState::kSuccess;
        case MqttControlPacketType::SUBSCRIBE: {
            size_t payload_length;
            uint16_t topic_filter_length;
            uint8_t subscription_options;

            if ((result->header_fields.find("remaining_length") == result->header_fields.end()) ||
                (result->header_fields.find("variable_header_length") == result->header_fields.end())) {
                return ParseState::kInvalid;
            }

            result->payload["topic_filter"] = "";
            result->payload["subscription_options"] = "";
            payload_length = (size_t)(result->header_fields["remaining_length"] - result->header_fields["variable_header_length"]);
            while (payload_length > 0) {
                PX_ASSIGN_OR_RETURN_ERROR(topic_filter_length, decoder->ExtractBEInt<uint16_t>());
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view topic_filter, decoder->ExtractString(topic_filter_length));
                if (result->payload["topic_filter"].empty()) {
                    result->payload["topic_filter"] += std::string(topic_filter);
                } else {
                    result->payload["topic_filter"] += ", " + std::string(topic_filter);
                }
                PX_ASSIGN_OR_RETURN_ERROR(subscription_options, decoder->ExtractBEInt<uint8_t>());
                result->payload["subscription_options"] += "{maximum_qos : " + std::to_string(subscription_options & 0x3) +
                                                            ", no_local : " + std::to_string((subscription_options >> 2) & 0x1) +
                                                            ", retain_as_published : " + std::to_string((subscription_options >> 3) & 0x1) +
                                                            ", retain_handling : " + std::to_string((subscription_options >> 4) & 0x3) + "}";
                payload_length -= (3 + topic_filter_length);
            }
            return ParseState::kSuccess;
        }
        case MqttControlPacketType::UNSUBSCRIBE: {
            size_t payload_length;
            uint16_t topic_filter_length;

            if ((result->header_fields.find("remaining_length") == result->header_fields.end()) ||
                (result->header_fields.find("variable_header_length") == result->header_fields.end())) {
                return ParseState::kInvalid;
            }

            result->payload["topic_filter"] = "";
            payload_length = (size_t)(result->header_fields["remaining_length"] - result->header_fields["variable_header_length"]);
            while (payload_length > 0) {
                PX_ASSIGN_OR_RETURN_ERROR(topic_filter_length, decoder->ExtractBEInt<uint16_t>());
                PX_ASSIGN_OR_RETURN_ERROR(std::string_view topic_filter, decoder->ExtractString(topic_filter_length));
                if (result->payload["topic_filter"].empty()) {
                    result->payload["topic_filter"] += std::string(topic_filter);
                } else {
                    result->payload["topic_filter"] += ", " + std::string(topic_filter);
                }
                payload_length -= (2 + topic_filter_length);
            }
            return ParseState::kSuccess;
        }
        case MqttControlPacketType::SUBACK:
        case MqttControlPacketType::UNSUBACK: {
            size_t payload_length;
            uint8_t reason_code;

            if ((result->header_fields.find("remaining_length") == result->header_fields.end()) ||
                (result->header_fields.find("variable_header_length") == result->header_fields.end())) {
                return ParseState::kInvalid;
            }

            result->payload["reason_code"] = "";
            payload_length = (size_t)(result->header_fields["remaining_length"] - result->header_fields["variable_header_length"]);
            while (payload_length > 0) {
                PX_ASSIGN_OR_RETURN_ERROR(reason_code, decoder->ExtractBEInt<uint8_t>());
                if (result->payload["reason_code"].empty()) {
                    result->payload["reason_code"] += std::to_string(reason_code);
                } else {
                    result->payload["reason_code"] += ", " + std::to_string(reason_code);
                }
                payload_length -= 1;
            }
            return ParseState::kSuccess;
        }
        case MqttControlPacketType::PINGREQ:
        case MqttControlPacketType::PINGRESP:
        case MqttControlPacketType::DISCONNECT:
            return ParseState::kSuccess;
        default:
            return ParseState::kInvalid;;
    }
}

ParseState ParseFrame(message_type_t type, std::string_view* buf,
                  Message* result) {
    CTX_DCHECK(type == message_type_t::kRequest || type == message_type_t::kResponse);
    if (buf->size() < 2) {
        return ParseState::kNeedsMoreData;
    }

    BinaryDecoder decoder(*buf);

    // Parsing the fixed header
    // Control Packet Type extracted from first four bits of the first byte
    PX_ASSIGN_OR_RETURN_ERROR(uint8_t control_packet_code_flags, decoder.ExtractBEInt<uint8_t>());
    uint8_t control_packet_code = control_packet_code_flags >> 4;
    uint8_t control_packet_flags = control_packet_code_flags & 0x0F;

    MqttControlPacketType control_packet_type = GetControlPacketType(control_packet_code);
    result->control_packet_type = ControlPacketTypeStrings[control_packet_type];

    // Saving the flags if control packet type is PUBLISH
    if (control_packet_type == MqttControlPacketType::PUBLISH) {
        result->header_fields["dup"] = (control_packet_flags >> 3) != 0;
        result->header_fields["retain"] = (control_packet_flags & 0x1) != 0;
        result->header_fields["qos"] = (control_packet_flags >> 1) & 0x3;
    }

    // Decoding the variable encoding of remaining length field
    size_t remaining_length;
    std::tie(remaining_length, std::ignore) = VariableLengthEncodingDecoder(&decoder);
    if (remaining_length < 0) {
        return ParseState::kInvalid;
    }
    result->header_fields["remaining_length"] = remaining_length;

    if (ParseVariableHeader(result, &decoder, control_packet_type) == ParseState::kInvalid) {
        return ParseState::kInvalid;
    }

    if (ParsePayload(result, &decoder, control_packet_type) == ParseState::kInvalid) {
        return ParseState::kInvalid;
    }

    *buf = decoder.Buf();
    return ParseState::kSuccess;
}

} // namespace mqtt

template<>
ParseState ParseFrame(message_type_t type, std::string_view* buf,
                      mqtt::Message* result, NoState* /*state*/) {
    return mqtt::ParseFrame(type, buf, result);
}

template <>
size_t FindFrameBoundary<mqtt::Message>(message_type_t /*type*/, std::string_view buf,
                                     size_t start_pos, NoState* /*state*/) {
    return start_pos + buf.length();
}

} // namespace protocols
} // namespace stirling
} // namespace px