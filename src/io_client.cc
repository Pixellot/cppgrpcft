#include "io_client.h"

#include <string>
#include <stdexcept>
#include <memory>
#include <sstream>
#include <fstream>
#include <cstdio>

#include <grpcpp/grpcpp.h>

#include <google/protobuf/any.pb.h>

#include "../proto/io.grpc.pb.h"

#include "io_interfaces.h"

void BytesTransferClient::Receive(const google::protobuf::Any& streamerMsg, const google::protobuf::Any& receiverMsg, BytesReceiver* receiver, ::grpc::ClientContext* context) {

    if (receiver == nullptr) {
        throw std::runtime_error( "uninitialized bytes receiver");
    }

    try {
        receiver->init(receiverMsg);
    }
    catch(const std::exception& ex) {
        std::stringstream ss;
        ss << "bytes receiver initialization failed: " << ex.what();
        throw std::runtime_error(ss.str());
    }

    ::Io::Info info;
    info.mutable_msg()->CopyFrom(streamerMsg);
    std::unique_ptr<::grpc::ClientReader<::Io::Chunk>> reader(stub_->Receive(context, info));

    ::Io::Chunk chunk;
    while (reader->Read(&chunk)) {
        std::string data = chunk.data();
        try {
            receiver->push(data);
        }
        catch(const std::exception& ex) {
            std::stringstream ss;
            ss << "failed to push bytes: " << ex.what();
            throw std::runtime_error(ss.str());
        }
    }

    try {
        receiver->finalize();
    }
    catch(const std::exception& ex) {
        std::stringstream ss;
        ss << "bytes receiver finalize failed: " << ex.what();
        throw std::runtime_error(ss.str());
    }

    ::grpc::Status status = reader->Finish();
    if (!status.ok()) {
        std::stringstream ss;
        ss << "client failed - code: " << status.error_code() << ", message: " << status.error_message();
        throw std::runtime_error(ss.str());
    }
}

void BytesTransferClient::Send(const google::protobuf::Any& streamerMsg, const google::protobuf::Any& receiverMsg, BytesStreamer* streamer, ::grpc::ClientContext* context) {

    if (streamer == nullptr) {
        throw std::runtime_error("uninitialized bytes streamer");
    }

    try {
        streamer->init(streamerMsg);
    }
    catch(const std::exception& ex) {
        std::stringstream ss;
        ss << "bytes streamer initialization failed: " << ex.what();
        throw std::runtime_error(ss.str());
    }

    ::Io::Status ioStatus;
    std::unique_ptr<::grpc::ClientWriter<::Io::Packet>> writer(stub_->Send(context, &ioStatus));

    ::Io::Packet header;
    header.mutable_info()->mutable_msg()->CopyFrom(receiverMsg);
    if(!writer->Write(header))
    {
        writer->WritesDone();

        ::grpc::Status status = writer->Finish();
        if (!status.ok()) {
            std::stringstream ss;
            ss << "client failed - code: " << status.error_code() << ", message: " << status.error_message() << ", io status: (success: " << ioStatus.success() << ", msg: " << ioStatus.desc() << ')';
            throw std::runtime_error(ss.str());
        }
    }

    while (streamer->hasNext()) {
        try {
            ::Io::Packet payload;
            payload.mutable_chunk()->set_data(streamer->getNext());
            if(!writer->Write(payload)) {
                break;
            }
        }
        catch(const std::exception& ex) {
            std::stringstream ss;
            ss << "failed to get next streamer bytes: " << ex.what();
        }
    }

    try {
        streamer->finalize();
    }
    catch(const std::exception& ex) {
        std::stringstream ss;
        ss << "bytes streamer finalize failed: " << ex.what();
        throw std::runtime_error(ss.str());
    }

    writer->WritesDone();

    ::grpc::Status status = writer->Finish();
    if (!status.ok()) {
        std::stringstream ss;
        ss << "client failed - code: " << status.error_code() << ", message: " << status.error_message() << ", io status: (success: " << ioStatus.success() << ", msg: " << ioStatus.desc() << ')';
        throw std::runtime_error(ss.str());
    }
}

