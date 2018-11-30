#include "RtpH264Pack.h"
#include <string.h>
#include <stdio.h>
#include "LogUtil.h"
RTPH264Pack::RTPH264Pack(const uint32_t ssrc,
	const uint8_t payload_type,
	const uint16_t rtp_packet_max_size):
	rtp_packet_max_size_(rtp_packet_max_size)
{
	if (rtp_packet_max_size_ > RTP_PACKET_MAX_SIZE)
	{
		rtp_packet_max_size_ = RTP_PACKET_MAX_SIZE;		// �������
	}
	memset(&rtp_info_, 0, sizeof(rtp_info_));

	rtp_info_.rtp_hdr.set_type(payload_type);
	rtp_info_.rtp_hdr.set_ssrc(ssrc);
	rtp_info_.rtp_hdr.set_version(RTP_VERSION);
	rtp_info_.rtp_hdr.set_seq_num(0);
}

RTPH264Pack::~RTPH264Pack()
{
}

//����Set�����ݱ�����һ��������NAL,��ʼ��Ϊ0x00000001��
//��ʼ��֮ǰ����Ԥ��10���ֽڣ��Ա����ڴ�COPY������
//�����ɺ�ԭ�������ڵ����ݱ��ƻ���
bool RTPH264Pack::Pack(uint8_t * p_nal_buf, uint32_t nal_buf_size, uint32_t timestamp, bool end_of_frame)
{
	uint32_t startcode = StartCode(p_nal_buf);

	if (startcode != 0x01000000)
	{
		return false;
	}

	if (nal_buf_size < (4 + 1))	// С��startcode 4�ֽ� + nal type 1�ֽ�
	{
		return false;		
	}

	rtp_info_.nal.start = p_nal_buf;
	rtp_info_.nal.size = nal_buf_size;
	rtp_info_.nal.b_end_of_frame = end_of_frame;
	rtp_info_.nal.type = rtp_info_.nal.start[4];
	rtp_info_.nal.end = rtp_info_.nal.start + rtp_info_.nal.size;

	rtp_info_.rtp_hdr.timestamp = timestamp;		// ʱ���

	rtp_info_.nal.start += 4;	// skip the syncword

	// -4Ϊstart code���ȣ�-1Ϊnal type����
	if ((rtp_info_.nal.size - 4 - 1 + RTP_HEADER_LEN) > rtp_packet_max_size_)
	{
		rtp_info_.FU_flag = true;		// ��ʱ��Ҫ���ֶ��RTP��
		rtp_info_.s_bit = 1;
		rtp_info_.e_bit = 0;
		// ������RTP��ʱ����Ҫ�ٴ� NAL HEADER����FU Header����
		rtp_info_.nal.start += 1;	// skip NAL header	
	}
	else
	{
		rtp_info_.FU_flag = false;	// ����Ҫ����RTP��
		rtp_info_.s_bit = rtp_info_.e_bit = 0;
	}

	rtp_info_.start = rtp_info_.end = rtp_info_.nal.start;
	b_begin_nal_ = true;

	return true;
}

uint8_t * RTPH264Pack::GetPacket(int &out_packet_size)
{
	if (rtp_info_.end == rtp_info_.nal.end)
	{
		out_packet_size = 0;		// �����Ѿ���ȡ�����
		return NULL;
	}

	if (b_begin_nal_)
	{
		b_begin_nal_ = false;
	}
	else
	{
		rtp_info_.start = rtp_info_.end;	// continue with the next RTP-FU packet
	}

	int bytes_left = rtp_info_.nal.end - rtp_info_.start;
	int max_size = rtp_packet_max_size_ - RTP_HEADER_LEN;	// sizeof(basic rtp header) == 12 bytes
	if (rtp_info_.FU_flag)
		max_size -= 2;		// һ�ֽ�Ϊ FU indicator����һ�ֽ�Ϊ FU header

	if (bytes_left > max_size)	
	{
		rtp_info_.end = rtp_info_.start + max_size;	// limit RTP packet size to max_size bytes
	}
	else
	{
		rtp_info_.end = rtp_info_.start + bytes_left;
	}

	if (rtp_info_.FU_flag)
	{	// multiple packet NAL slice
		if (rtp_info_.end == rtp_info_.nal.end)		// ˵���Ѿ������һ��FU ��Ƭ
		{
			rtp_info_.e_bit = 1;	// �����ó�1, ����λָʾ��ƬNAL��Ԫ�Ľ���
		}
	}

	// �Ƚ��Ƿ�������һ֡ͼ�����ݵĽ���
	rtp_info_.rtp_hdr.marker = rtp_info_.nal.b_end_of_frame ? 1 : 0; // should be set at EofFrame
	if (rtp_info_.FU_flag && !rtp_info_.e_bit)	// ������Ƭʱ��Ҫ����  e_bit
	{
		rtp_info_.rtp_hdr.marker = 0;
	}
	

	rtp_info_.rtp_hdr.sequencenumber++;		// ���������������
	int type = rtp_info_.nal.type;
	if (/*0x65 == type || */0x67 == type || 0x68 == type)
	{
		LogDebug("type:0x%x, seq;%d", type, rtp_info_.rtp_hdr.sequencenumber);
	}
	// ��ʼ��װRTP packet
	uint8_t *cp = rtp_info_.start;	//��ȡ
	cp -= (rtp_info_.FU_flag ? 14 : 12);
	rtp_info_.p_rtp = cp;

	uint8_t *cp2 = (uint8_t *)&rtp_info_.rtp_hdr;
	cp[0] = cp2[0];
	cp[1] = cp2[1];

	cp[2] = (rtp_info_.rtp_hdr.sequencenumber >> 8) & 0xff;	// �ȸ߰�λ
	cp[3] = rtp_info_.rtp_hdr.sequencenumber & 0xff;

	cp[4] = (rtp_info_.rtp_hdr.timestamp >> 24) & 0xff;	// �ȸ߰�λ
	cp[5] = (rtp_info_.rtp_hdr.timestamp >> 16) & 0xff;
	cp[6] = (rtp_info_.rtp_hdr.timestamp >> 8) & 0xff;
	cp[7] = rtp_info_.rtp_hdr.timestamp & 0xff;

	cp[8] = (rtp_info_.rtp_hdr.ssrc >> 24) & 0xff;
	cp[9] = (rtp_info_.rtp_hdr.ssrc >> 16) & 0xff;
	cp[10] = (rtp_info_.rtp_hdr.ssrc >> 8) & 0xff;
	cp[11] = rtp_info_.rtp_hdr.ssrc & 0xff;
	rtp_info_.hdr_len = RTP_HEADER_LEN;
	/*!
	* /n The FU indicator octet has the following format:
	* /n
	* /n      +---------------+
	* /n MSB  |0|1|2|3|4|5|6|7|  LSB
	* /n      +-+-+-+-+-+-+-+-+
	* /n      |F|NRI|  Type   |
	* /n      +---------------+
	* /n
	* /n The FU header has the following format:
	* /n
	* /n      +---------------+
	* /n      |0|1|2|3|4|5|6|7|
	* /n      +-+-+-+-+-+-+-+-+
	* /n      |S|E|R|  Type   |
	* /n      +---------------+
	*/
	if (rtp_info_.FU_flag)
	{
		// FU indicator  F|NRI|Type
		cp[RTP_HEADER_LEN] = (rtp_info_.nal.type & 0xe0) | 28;	//Type is 28 for FU_A
													//FU header		S|E|R|Type
		cp[13] = (rtp_info_.s_bit << 7) | (rtp_info_.e_bit << 6) | (rtp_info_.nal.type & 0x1f); //R = 0, must be ignored by receiver

		rtp_info_.s_bit = rtp_info_.e_bit = 0;	// ����
		rtp_info_.hdr_len = 14;
	}
	rtp_info_.start = &cp[rtp_info_.hdr_len];	// new start of payload

	out_packet_size = rtp_info_.hdr_len + (rtp_info_.end - rtp_info_.start);
	return rtp_info_.p_rtp;
}

RTPH264Pack::RTP_INFO_T RTPH264Pack::GetRtpInfo() const
{
	return rtp_info_;
}

void RTPH264Pack::SetRtpInfo(const RTP_INFO_T & RTP_Info)
{
	rtp_info_ = RTP_Info;
}

unsigned int RTPH264Pack::StartCode(uint8_t * cp)
{
	unsigned int d32;
	d32 = cp[3];
	d32 <<= 8;
	d32 |= cp[2];
	d32 <<= 8;
	d32 |= cp[1];
	d32 <<= 8;
	d32 |= cp[0];
	return d32;
}


RTPH264Unpack::RTPH264Unpack(uint8_t H264PAYLOADTYPE)
	: b_sps_found_(false)
	, b_found_key_frame_(false)
	, b_pre_frame_finish_(false)
	, seq_num_(0)
	, ssrc_(0)
	, resync_(true)
{
	receive_buf_data_ = new uint8_t[BUF_SIZE];

	h264_playload_type_ = H264PAYLOADTYPE;
	receive_buf_end_ = receive_buf_data_ + BUF_SIZE;
	receive_buf_start_ = receive_buf_data_;
	cur_receive_size_ = 0;
}


RTPH264Unpack::~RTPH264Unpack(void)
{
	delete[] receive_buf_data_;
}

//p_bufΪH264 RTP��Ƶ���ݰ���buf_sizeΪRTP��Ƶ���ݰ��ֽڳ��ȣ�out_sizeΪ�����Ƶ����֡�ֽڳ��ȡ�
//����ֵΪָ����Ƶ����֡��ָ�롣�������ݿ��ܱ��ƻ���
/**
 * 1. ����RTP Header
 * 2. ���汾�Ƿ�һ��
 * 3. ����SSRC(Synchronization source)�Ƿ�һ�£������һ��������
 * 4. ����PPS SPS�Ƿ��Ѿ��ҵ�
 * 5. ����I֡�Ƿ��Ѿ��ҵ�
 * 6. ��������֡���͵����ݣ���ȥSPS/PPS/I֡�ģ�
 */
uint8_t *RTPH264Unpack::ParseRtpPacket(uint8_t * p_buf, uint16_t buf_size, int &out_size, uint32_t &timestamp)
{
	if (buf_size <= RTP_HEADER_LEN)
		return NULL;
	memset(&rtp_header_, 0, RTP_HEADER_LEN);
	uint8_t *cp = (uint8_t *)&rtp_header_;

	cp[0] = p_buf[0];
	cp[1] = p_buf[1];
	
	rtp_header_.seq = p_buf[2];
	rtp_header_.seq <<= 8;
	rtp_header_.seq |= p_buf[3];
	
	rtp_header_.ts = p_buf[4];
	rtp_header_.ts <<= 8;
	rtp_header_.ts |= p_buf[5];
	rtp_header_.ts <<= 8;
	rtp_header_.ts |= p_buf[6];
	rtp_header_.ts <<= 8;
	rtp_header_.ts |= p_buf[7];

	rtp_header_.ssrc = p_buf[8];
	rtp_header_.ssrc <<= 8;
	rtp_header_.ssrc |= p_buf[9];
	rtp_header_.ssrc <<= 8;
	rtp_header_.ssrc |= p_buf[10];
	rtp_header_.ssrc <<= 8;
	rtp_header_.ssrc |= p_buf[11];

	// ������pps sps֡������Ҫ���֮ǰ��һ֡�Ƿ��Ѿ��������

	
	

	// Check the RTP version number (it should be 2):
	// 2 ���أ��������� RTP �İ汾����Э�鶨��İ汾�� 2��
	if (rtp_header_.v != RTP_VERSION)
	{
		//LogError("rtp_header_.v != RTP_VERSION");
		return NULL;
	}

	if (ssrc_ != rtp_header_.ssrc)		// �����ʱ˵�������л�����Ҫ����sync
	{
		ssrc_ = rtp_header_.ssrc;
		LogDebug("ssrc change....");
		resetPacket();
	}

	uint8_t *p_payload = p_buf + RTP_HEADER_LEN;
	int32_t payload_size = buf_size - RTP_HEADER_LEN;


	// Skip over any CSRC identifiers in the header:
	if (rtp_header_.cc)//4 ���أ�CSRC ���������˸��ڹ̶�ͷ���� CSRC ʶ�������Ŀ��
	{
		uint32_t cc = rtp_header_.cc * 4;	// һ��CSRCռ��4���ֽ�
		if (payload_size < cc)
		{
			LogError("payload_size < cc");
			return NULL;
		}
		payload_size -= cc;
		p_payload += cc;		// ����CSRC
	}

	// Check for (& ignore) any RTP header extension
	if (rtp_header_.x)	// 1 ���أ���������չ����,�̶�ͷ(��)�������һ��ͷ��չ��
	{
		if (payload_size < 4)
		{
			LogError("payload_size < cc");
			return NULL;
		}
		payload_size -= 4;
		p_payload += 2;
		uint32_t rtp_ext_size = p_payload[0];
		rtp_ext_size <<= 8;
		rtp_ext_size |= p_payload[1];
		p_payload += 2;
		rtp_ext_size *= 4;
		if (payload_size < rtp_ext_size)
			return NULL;
		payload_size -= rtp_ext_size;
		p_payload += rtp_ext_size;
	}

	// Discard any padding uint8_ts:
	if (rtp_header_.p)
	{
		if (payload_size == 0)
		{
			LogError("payload_size == 0");
			return NULL;
		}
		uint32_t padding = p_payload[payload_size - 1];
		if (payload_size < padding)
		{
			LogError("payload_size < padding");
			return NULL;
		}
		payload_size -= padding;
	}

	int payload_type = p_payload[0] & 0x1f;
	int nal_type = payload_type;
	if (RTP_H264_FU_A == nal_type) // FU_A
	{
		if (payload_size < 2)
		{
			LogError("payload_size < 2");
			return NULL;
		}
		nal_type = p_payload[1] & 0x1f;
	}

	if (RTP_H264_SPS == nal_type)	// SPS
	{
		//LogDebug("nal_type =%d", nal_type);
		b_sps_found_ = true;
	}
	if (!b_sps_found_)		// ��Ҫ�ȴ��ҵ�SPS���������޷�����
	{
		LogDebug("wait found sps frame, seq:%d", rtp_header_.seq);
		return NULL;
	}
	if (nal_type == 0x07 || nal_type == 0x08) // SPS PPS
	{
		seq_num_ = rtp_header_.seq;		// ����sequence num
		p_payload -= 4;
		*((uint32_t*)(p_payload)) = 0x01000000;
		out_size = payload_size + 4;
		LogDebug("nal_type:%d, out_size:%d, seq:%d", nal_type, out_size, seq_num_);
		b_pre_frame_finish_ = true;		// 
		return p_payload;
	}

	if (rtp_header_.seq != (uint16_t)(seq_num_ + 1)) // lost packet
	{
// 		b_pre_frame_finish_ = false;
// 		resetPacket();
 		LogError("rtp packet, preseq:%d, curseq:%d", seq_num_, rtp_header_.seq);
// 		return NULL;
	}
	seq_num_ = rtp_header_.seq;

	
	if (payload_type != RTP_H264_FU_A) // whole NAL
	{
		*((uint32_t*)(receive_buf_start_)) = 0x01000000;
		receive_buf_start_ += 4;
		cur_receive_size_ += 4;
	}
	else // FU_A
	{
		if (p_payload[1] & 0x80) // FU_A start
		{
			*((uint32_t*)(receive_buf_start_)) = 0x01000000;
			receive_buf_start_ += 4;
			cur_receive_size_ += 4;

			p_payload[1] = (p_payload[0] & 0xE0) | nal_type;

			p_payload += 1;
			payload_size -= 1;
		}
		else
		{
			p_payload += 2;		// ���� FU indicator �� header
			payload_size -= 2;
		}
	}

	if (receive_buf_start_ + payload_size < receive_buf_end_)
	{
		memcpy(receive_buf_start_, p_payload, payload_size);
		cur_receive_size_ += payload_size;
		receive_buf_start_ += payload_size;
	}
	else // memory overflow
	{
		LogError("memory overflow");
		resetPacket();
		return NULL;
	}

	if (rtp_header_.m) // frame end || nal_type == 5
	{
		out_size = cur_receive_size_;
		receive_buf_start_ = receive_buf_data_;
		timestamp = rtp_header_.ts;
		cur_receive_size_ = 0;
		if ((RTP_H264_IDR  == nal_type) && b_pre_frame_finish_) // KEY FRAME
		{
			b_found_key_frame_ = true;
			LogDebug("b_found_key_frame_ = %d, seq:%d", b_found_key_frame_, seq_num_);
		}
		else
		{
			b_pre_frame_finish_ = true;
			if (!b_found_key_frame_)	// I֡��û���ҵ�����Ҫ��������Ŷ
			{
				// �����ʱ��û���ҵ���������sync pps sps I֡
				resetPacket();		// �����һ֡����I֡������Ҫ����
				LogError(" nal_type = 0x%x", nal_type);
				return NULL;
			}
		}
		
		return receive_buf_data_;
	}
	else
	{
		return NULL;
	}
}



void RTPH264Unpack::resetPacket()
{
// 	b_sps_found_ = false;
	resync_ = true;
	b_found_key_frame_ = false;
	m_bAssemblingFrame = false;
	receive_buf_start_ = receive_buf_data_;
	cur_receive_size_ = 0;
}