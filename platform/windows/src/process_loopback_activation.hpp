#pragma once

#include <Windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include <cstdint>
#include <memory>
#include <type_traits>

namespace catro::platform::windows::detail {

using Microsoft::WRL::ComPtr;
using Microsoft::WRL::FtmBase;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;
using Microsoft::WRL::ClassicCom;

class ActivationHandler final
    : public RuntimeClass<
          RuntimeClassFlags<ClassicCom>,
          FtmBase,
          IActivateAudioInterfaceCompletionHandler> {
public:
    ActivationHandler(std::uint32_t process_id, bool exclude_target) noexcept {
        activation_.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        activation_.ProcessLoopbackParams.TargetProcessId = process_id;
        activation_.ProcessLoopbackParams.ProcessLoopbackMode = exclude_target
            ? PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
            : PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        parameters_.vt = VT_BLOB;
        parameters_.blob.cbSize = sizeof(activation_);
        parameters_.blob.pBlobData = reinterpret_cast<BYTE*>(&activation_);
    }

    HANDLE completed() const noexcept { return completed_.get(); }
    PROPVARIANT* parameters() noexcept { return &parameters_; }
    HRESULT result() const noexcept { return result_; }
    ComPtr<IAudioClient> client() const noexcept { return client_; }

    STDMETHODIMP ActivateCompleted(
        IActivateAudioInterfaceAsyncOperation*
            operation) override {
        HRESULT activation = E_UNEXPECTED;
        ComPtr<IUnknown> unknown;
        auto result = operation != nullptr
            ? operation->GetActivateResult(
                  &activation, &unknown)
            : E_POINTER;
        if (SUCCEEDED(result)) {
            result = activation;
        }
        if (SUCCEEDED(result)) {
            result = unknown ? unknown.As(&client_) : E_NOINTERFACE;
        }
        result_ = result;
        SetEvent(completed_.get());
        return S_OK;
    }

private:
    // The OS retains this handler after a timeout/cancellation. No callback may access caller
    // stack memory or an event that the caller has already closed.
    std::unique_ptr<std::remove_pointer_t<HANDLE>, decltype(&CloseHandle)> completed_{
        CreateEventW(nullptr, TRUE, FALSE, nullptr), &CloseHandle};
    ComPtr<IAudioClient> client_;
    HRESULT result_ = E_PENDING;
    AUDIOCLIENT_ACTIVATION_PARAMS activation_{};
    PROPVARIANT parameters_{};
};

} // namespace catro::platform::windows::detail
