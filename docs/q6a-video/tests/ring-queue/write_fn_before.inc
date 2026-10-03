static int iris_hfi_queue_write(struct iris_iface_q_info *qinfo, void *packet, u32 packet_size)
{
	struct iris_hfi_queue_header *queue = qinfo->qhdr;
	u32 write_idx = queue->write_idx * sizeof(u32);
	u32 read_idx = queue->read_idx * sizeof(u32);
	u32 empty_space, new_write_idx, residue;
	u32 *write_ptr;

	if (write_idx < read_idx)
		empty_space = read_idx - write_idx;
	else
		empty_space = IFACEQ_QUEUE_SIZE - (write_idx -  read_idx);
	if (empty_space < packet_size)
		return -ENOSPC;

	queue->tx_req =  0;

	new_write_idx = write_idx + packet_size;
	write_ptr = (u32 *)((u8 *)qinfo->kernel_vaddr + write_idx);

	if (write_ptr < (u32 *)qinfo->kernel_vaddr ||
	    write_ptr > (u32 *)(qinfo->kernel_vaddr +
	    IFACEQ_QUEUE_SIZE))
		return -EINVAL;

	if (new_write_idx < IFACEQ_QUEUE_SIZE) {
		memcpy(write_ptr, packet, packet_size);
	} else {
		residue = new_write_idx - IFACEQ_QUEUE_SIZE;
		memcpy(write_ptr, packet, (packet_size - residue));
		memcpy(qinfo->kernel_vaddr,
		       packet + (packet_size - residue), residue);
		new_write_idx = residue;
	}

	/* Make sure packet is written before updating the write index */
	mb();
	queue->write_idx = new_write_idx / sizeof(u32);

	/* Make sure write index is updated before an interrupt is raised */
	mb();

	return 0;
}
