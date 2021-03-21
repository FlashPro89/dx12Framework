#include "RingUploadBuffer.h"
#include <assert.h>

ADDRESS _align( ADDRESS uLocation, ADDRESS uAlign )
{
    if ((0 == uAlign) || (uAlign & (uAlign - 1)))
    {
        assert( true && "non-pow2 alignment" );
    }
    return ((uLocation + (uAlign - 1)) & ~(uAlign - 1));
}

gRingUploadBuffer::gRingUploadBuffer( ComPtr<ID3D12Device> cpDevice ) :
    m_cpD3DDevice(cpDevice), m_uploadBufferSize(0), m_lpMappedData(0), m_currentBufferPos(0)
{
	assert(cpDevice.Get() != NULL);
}

gRingUploadBuffer::~gRingUploadBuffer()
{

}

bool gRingUploadBuffer::initialize( ADDRESS uploadBufferSize )
{
    CD3DX12_HEAP_PROPERTIES props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    CD3DX12_RESOURCE_DESC rdesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

    if(FAILED(m_cpD3DDevice->CreateCommittedResource( &props, D3D12_HEAP_FLAG_NONE,
             &rdesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, 
             IID_PPV_ARGS(&m_cpUploadBuffer) ) ) )
        return false;

    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
   
    // after map(), unmap() calling is not needed (Advanced Usage Models)
    if (FAILED(m_cpUploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&m_lpMappedData))))
        return false;

    m_uploadBufferSize = uploadBufferSize;
	return true;
}

void* gRingUploadBuffer::allocate( UINT frameId, ADDRESS size, ADDRESS align, ADDRESS* pOutOffset)
{
    void* lp = 0;
    
    if( m_uploadBufferSize < size )
        return lp;

    UINT64 _firstByte = reinterpret_cast<size_t>(m_lpMappedData);
    UINT64 _currentPos = m_uploadsQueue.empty() ? 0 : m_uploadsQueue.back().resourceEndOffset;
    UINT64 _alignedPos = _align(_currentPos + _firstByte, align) - _firstByte;

    if(m_uploadBufferSize - _alignedPos < size ) //разворот на начало буффера
    {
        _alignedPos = _align(_firstByte, align) - _firstByte;
        SIZE_T _availableSize = m_uploadsQueue.front().resourceStartOffset - _alignedPos;
        if ( _availableSize >= size )
        {
            m_uploadsQueue.push(RingUploadResourceBufferOffset(
                frameId, _alignedPos, size));
        }
        else
            return nullptr;
            //assert(m_uploadsQueue.empty() && "Allocation is out of bounds the unload buffer!");
    }

    if (pOutOffset)
        *pOutOffset = _alignedPos;
    m_uploadsQueue.push( RingUploadResourceBufferOffset( frameId, _alignedPos, size) );
    lp = reinterpret_cast<void*>(_alignedPos + _firstByte);

    return lp;
}

UINT64 gRingUploadBuffer::getAvailableAllocationSize(ADDRESS align) const
{
    UINT64 _firstByte = reinterpret_cast<size_t>(m_lpMappedData);
    UINT64 _currentPos = m_uploadsQueue.empty() ? 0 : m_uploadsQueue.back().resourceEndOffset;
    UINT64 _alignedPos = _align(_currentPos + _firstByte, align) - _firstByte;

    UINT64 _availableBackSize = m_uploadBufferSize - _alignedPos;

    if (!m_uploadsQueue.empty()) //разворот на начало буффера
    {
        _alignedPos = _align(_firstByte, align) - _firstByte;
        UINT64 _availableFrontSize = m_uploadsQueue.front().resourceStartOffset - _alignedPos;
        return _availableFrontSize > _availableBackSize ? _availableFrontSize : _availableBackSize;
    }

    return _availableBackSize;
}

void gRingUploadBuffer::frameEnded(UINT frameId)
{
    while (!m_uploadsQueue.empty()
        && m_uploadsQueue.front().frameId <= frameId)
    {
        m_uploadsQueue.pop();
    }
}

void gRingUploadBuffer::clearQueue()
{
    while( !m_uploadsQueue.empty() )
        m_uploadsQueue.pop();
}

ID3D12Resource* gRingUploadBuffer::getResource() const
{
    return m_cpUploadBuffer.Get();
}

size_t gRingUploadBuffer::getQueueSize() const
{
    return m_uploadsQueue.size();
}


