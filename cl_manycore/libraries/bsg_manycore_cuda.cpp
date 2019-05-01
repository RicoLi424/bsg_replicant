#include <bsg_manycore_cuda.h>  
#include <bsg_manycore_driver.h>
#include <bsg_manycore_tile.h>
#include <bsg_manycore_memory_manager.h>
#include <bsg_manycore_elf.h>
#include <bsg_manycore_mem.h>
#include <bsg_manycore_loader_dep.h>

static const uint32_t KERNEL_REG = 0x1000 >> 2; //!< EPA of kernel. 
static const uint32_t ARGC_REG = 0x1004 >> 2; //!< EPA of number of arguments kernel will use. 
static const uint32_t ARGV_REG = 0x1008 >> 2; //!< EPA of arguments for kernel. 
static const uint32_t SIGNAL_REG = 0x100c >> 2; //!< EPA of register that holds signal address. Tile will write to this address once it completes the kernel.   


static const uint32_t FINISH_ADDRESS = 0xC0DA; //!< EPA to which tile group sends a finish packet once it finishes executing a kernel  

static awsbwhal::MemoryManager *mem_manager[1] = {(awsbwhal::MemoryManager *) 0}; /* This array has an element for every EVA <-> NPA mapping. Currently, only one mapping is supported. */

static uint32_t const DRAM_SIZE = 0x80000000;

/*!
 * Gets the x coordinates of a list of tile_t structs.
 * @param tiles array of tiles. Must be allocated by the caller.
 * @param x_list array of x coordinates. Must be allocated by the caller.
 * @param num_tiles array number of tiles.
 * @return HB_MC_SUCCESS on success and HB_MC_FAIL otherwise. 
 */
static int hb_mc_get_x(tile_t *tiles, uint8_t *x_list, uint32_t num_tiles) {
	if (!tiles || !x_list) {
		return HB_MC_FAIL;
	}
	for (int i = 0; i < num_tiles; i++) {
		x_list[i] = tiles[i].x;
	}
	return HB_MC_SUCCESS;
}

/*!
 * Gets the x coordinates of a list of tile_t structs.
 * @param tiles array of tiles. Must be allocated by the caller.
 * @param x_list array of x coordinates. Must be allocated by the caller.
 * @param num_tiles array number of tiles.
 * @return HB_MC_SUCCESS on success and HB_MC_FAIL otherwise. 
 */
static int hb_mc_get_y(tile_t *tiles, uint8_t *y_list, uint32_t num_tiles) {
	if (!tiles || !y_list) {
		return HB_MC_FAIL;
	}
	for (int i = 0; i < num_tiles; i++) {
		y_list[i] = tiles[i].y;
	}
	return HB_MC_SUCCESS;
}

/*!
 * creates a awsbwhal::MemoryManager object and stores it in a global table.
 * @param eva_id which specifies which EVA <-> NPA mapping.
 * @param elf path to ELF binary
 * @return HB_MC_SUCCESS on success and HB_MC_FAIL on failure.
 */
static int hb_mc_create_memory_manager (eva_id_t eva_id, char *elf) {
	eva_t program_end;
	if (symbol_to_eva(elf, "_bsg_dram_end_addr", &program_end) != HB_MC_SUCCESS) {
		return HB_MC_FAIL;
	}
	uint32_t alignment = 32;
	uint32_t start = program_end + alignment - (program_end % alignment); /* start at the next aligned block */
	uint32_t size = DRAM_SIZE;
	mem_manager[eva_id] = new awsbwhal::MemoryManager(DRAM_SIZE, start, alignment); 
	return HB_MC_SUCCESS;	
}

static int hb_mc_write_tile_reg(uint8_t fd, eva_t eva_id, tile_t *tile, uint32_t epa, uint32_t val) {
	int error =  hb_mc_copy_to_epa (fd, tile->x, tile->y, epa, &val, 1);
	if (error != HB_MC_SUCCESS)
		return HB_MC_FAIL; /* could not memcpy */
	return HB_MC_SUCCESS;	
}

/*
 * Takes in a device_t struct and initializes a grid of tile in the Manycore device.
 * @param[in] device points to the device. 
 * @param[in] dim_x/y determines the dimensions of grid. 
 * @param[in] origin_x/y determines the origin tile in the grid.
 * @return HB_MC_SUCCESS on success and HB_MC_FAIL on failure. 
 */
int hb_mc_grid_init (device_t *device, uint8_t dim_x, uint8_t dim_y, uint8_t origin_x, uint8_t origin_y){ 
	if (hb_mc_check_device(device->fd) != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_grid_init() --> hb_mc_check_device(): failed to verify device.\n"); 
		return HB_MC_FAIL;
	}
	
	uint32_t device_dim_x, device_dim_y;
	int error;

	error = hb_mc_get_config(device->fd, HB_MC_CONFIG_DEVICE_DIM_X, &device_dim_x); 
	if (error != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_grid_init() --> hb_mc_get_config(): failed to get device X dimension.\n");
		return HB_MC_FAIL;
	}

	error = hb_mc_get_config(device->fd, HB_MC_CONFIG_DEVICE_DIM_Y, &device_dim_y); 
	if (error != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_grid_init() --> hb_mc_get_config(): failed to get device Y dimension.\n");
		return HB_MC_FAIL;
	}

	if (dim_x <= 0){
		fprintf (stderr, "hb_mc_init_grid(): Grid X dimension (%d) not valid.\n", dim_x); 
		return HB_MC_FAIL;
	}
	if (dim_y <= 0){
		fprintf (stderr, "hb_mc_init_grid(): Grid Y dimension (%d) not valid.\n", dim_y);
		return HB_MC_FAIL;
	}
	if (dim_x > device_dim_x){
		fprintf (stderr, "hb_mc_init_grid(): Grid X dimension (%d) larger than device X dimension.\n", dim_x, device_dim_x); 
		return HB_MC_FAIL;
	}
	if (dim_y > device_dim_y){
		fprintf (stderr, "hb_mc_inti_grid(): Grid Y dimension (%d) larger than device Y dimension.\n", dim_y, device_dim_y);
		return HB_MC_FAIL;
	}

	tile_t* tiles = new tile_t [dim_x * dim_y];
	for (int x = origin_x; x < origin_x + dim_x; x++){
		for (int y = origin_y; y < origin_y + dim_y; y++){
			int tile_id = (y - origin_y) * dim_x + (x - origin_x);
			tiles[tile_id].x = x;
			tiles[tile_id].y = y;
			tiles[tile_id].origin_x = origin_x;
			tiles[tile_id].origin_y = origin_y;
			tiles[tile_id].tile_group_id = -1;
			tiles[tile_id].free = 1;
		}
	}

	grid_t *grid = new grid_t; 
	grid->dim_x = dim_x;
	grid->dim_y = dim_y;
	grid->origin_x = origin_x;
	grid->origin_y = origin_y;
	grid->tiles= (tile_t*)tiles;

	device->grid = grid;

	return HB_MC_SUCCESS;	
}

/*
 * Searches for a free tile group inside the device grid and allocoates it, and sets the dimensions, origin, and id of tile group.
 * @params[in] device and tg point to the device and tile group structures.
 * @param[in] id is the tile group id.
 * @paramsi[in] dim_x,y determine the dimensions of requested tile group.
 * returns HB_MC_SUCCESS on successful allocation and HB_MC_FAIL on fail.
 * */	
int hb_mc_tile_group_allocate (device_t *device, tile_group_t *tg, tile_group_id_t id, uint8_t dim_x, uint8_t dim_y){
	if (dim_x > device->grid->dim_x){
		fprintf(stderr, "hb_mc_tile_group_allocate(): tile group X dimension (%d) larger than grid X dimension (%d).\n", dim_x, device->grid->dim_x);
		return HB_MC_FAIL;
	}
	if (dim_y > device->grid->dim_y){
		fprintf(stderr, "hb_mc_tile_group_allocate(): tile group Y dimension (%d) larger than grid Y dimension (%d).\n", dim_y, device->grid->dim_y);
		return HB_MC_FAIL;
	}
	for (int org_y = device->grid->origin_y; org_y <= (device->grid->origin_y + device->grid->dim_y - dim_y); org_y++){
		for (int org_x = device->grid->origin_x; org_x <= (device->grid->origin_x + device->grid->dim_x - dim_x); org_x++){
			int free = 1;
			int tile_id;
			for (int x = org_x; x < org_x + dim_x; x++){
				for (int y = org_y; y < org_y + dim_y; y++){
					tile_id = (y - device->grid->origin_y) * device->grid->dim_x + (x - device->grid->origin_x);
					free = free & device->grid->tiles[tile_id].free;
				}
			}
			if (free){
				for (int x = org_x; x < org_x + dim_x; x++){
					for (int y = org_y; y < org_y + dim_y; y++){
						tile_id = (y - device->grid->origin_y) * device->grid->dim_x + (x - device->grid->origin_x);
						device->grid->tiles[tile_id].origin_x = org_x;
						device->grid->tiles[tile_id].origin_y = org_y;
						device->grid->tiles[tile_id].tile_group_id = id;
						device->grid->tiles[tile_id].free = 0;

						if (hb_mc_tile_set_origin(device->fd, device->grid->tiles[tile_id].x, device->grid->tiles[tile_id].y, device->grid->tiles[tile_id].origin_x, device->grid->tiles[tile_id].origin_y) != HB_MC_SUCCESS){
							fprintf (stderr, "hb_mc_tile_group_allocate() --> hb_mc_tile_set_origin(): failed to set tile group origin.\n");
							return HB_MC_FAIL;
						}
						if (hb_mc_tile_set_cord(device->fd, device->eva_id, device->elf, device->grid->tiles[tile_id].x, device->grid->tiles[tile_id].y, (x - org_x), (y - org_y)) != HB_MC_SUCCESS){
							fprintf(stderr, "hb_mc_tile_group_allocate() --> hb_mc_tile_set_cord(): failed to set tile coordinates.\n");
							return HB_MC_FAIL;
						}
					}
				}
		
				tg->id = id;
				tg->origin_x = org_x;
				tg->origin_y = org_y;
				tg->dim_x = dim_x;
				tg->dim_y = dim_y;

				fprintf(stderr, "%dx%d tile group %d allocated at origin (%d,%d).\n", tg->dim_x, tg->dim_y, tg->id, tg->origin_x, tg->origin_y);	
				return HB_MC_SUCCESS;
			}
		}
	}
	return HB_MC_FAIL;
}

/*
 * Takes the kernel name, argc, argv* and the finish signal address, and initializes a kernel and passes it to tilegroup.
 * @param[in] *device device pointer.
 * @param[in] *tg points to the tile group structure.
 * @param[in] name pointers to the name of the kernel.
 * @param[in] argc number of input arguments for the kernel.
 * @param[in] *argv pointer to the arguments in memory.
 * @param[in] finish_signal_addr is the address that the tilegroup will writes its finish signal into. 
 * @return HB_MC_SUCCESS if tile group is initialized sucessfuly and HB_MC_FAIL otherwise.
 * */	
int hb_mc_tile_group_init (device_t* device, tile_group_t* tg, char* name, uint32_t argc, uint32_t argv[], uint32_t finish_signal_addr) {
	if (hb_mc_check_device(device->fd) != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_tile_group_init() --> hb_mc_check_device(): failed to verify device.\n"); 
		return HB_MC_FAIL;
	}

	kernel_t *kernel = new kernel_t;
	kernel->name = name;
	kernel->argc = argc;
	kernel->argv = argv;
	kernel->finish_signal_addr = finish_signal_addr;
	tg->kernel = kernel; 
	return HB_MC_SUCCESS;
}

/* 
 * Launches a tile group by sending packets to each tile in the tile group setting the argc, argv, finish_addr and kernel pointer.
 * @param[in] device device pointer.
 * @parma[in] tg tile group pointer.
 * @return HB_MC_SUCCESS if tile group is launched successfully and HB_MC_FAIL otherwise.
 * */
int hb_mc_tile_group_launch (device_t *device, tile_group_t *tg) {
	if (hb_mc_check_device(device->fd) != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_check_device(): failed to verify device.\n"); 
		return HB_MC_FAIL;
	}

	eva_t args_eva;
	int error = hb_mc_device_malloc (device, (tg->kernel->argc) * sizeof(uint32_t), &args_eva); /* allocate device memory for arguments */
	if (error != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_device_malloc(): failed to allocate space on device of tile group %d arguments.\n", tg->id);
		return HB_MC_FAIL;
	}

	error = hb_mc_device_memcpy(device, reinterpret_cast<void *>(args_eva), (void *) &(tg->kernel->argv[0]), (tg->kernel->argc) * sizeof(uint32_t), hb_mc_memcpy_to_device); /* transfer the arguments to dram */
	if (error != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_device_memcpy(): failed to copy tile group %d arguments to device.\n", tg->id); 
		return HB_MC_FAIL;
	}
	
	eva_t kernel_eva; 
	error = symbol_to_eva(device->elf, tg->kernel->name, &kernel_eva); /* get EVA of kernel */
	if (error != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_symbol_to_eva(): invalid kernel name %s for tile group %d.\n", tg->kernel->name, tg->id); 
		return HB_MC_FAIL;
	}

	int tile_id;
	for (int y = tg->origin_y; y < tg->origin_y + tg->dim_y; y++){
		for (int x = tg->origin_x; x < tg->origin_x + tg->dim_x; x++){
			tile_id = (y - device->grid->origin_y) * device->grid->dim_x + (x - device->grid->origin_x);


			error = hb_mc_write_tile_reg(device->fd, device->eva_id, &(device->grid->tiles[tile_id]), ARGC_REG, tg->kernel->argc); /* write argc to tile */
			if (error != HB_MC_SUCCESS) {
				fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_write_tile_reg(): failed to write argc %d to device for tile group %d.\n", tg->kernel->argc, tg->id); 
				return HB_MC_FAIL; 
			}
		

			error = hb_mc_write_tile_reg(device->fd, device->eva_id, &(device->grid->tiles[tile_id]), ARGV_REG, args_eva); /* write EVA of arguments to tile group */
			if (error != HB_MC_SUCCESS) {
				fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_write_tile_reg(): failed to write argv to device for tile group %d.\n", tg->id);
				return HB_MC_FAIL; 
			}

			uint32_t host_coord_x, host_coord_y;
			error = hb_mc_get_config(device->fd, HB_MC_CONFIG_DEVICE_HOST_INTF_COORD_X, &host_coord_x);
			if (error != HB_MC_SUCCESS) {
				fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_get_config(): failed to get device-host interface X coordiante.\n");
				return HB_MC_FAIL;
			}

			error = hb_mc_get_config(device->fd, HB_MC_CONFIG_DEVICE_HOST_INTF_COORD_Y, &host_coord_y);
			if (error != HB_MC_SUCCESS) {
				fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_get_config(): failed to get device-host interface Y coordiante.\n");
				return HB_MC_FAIL;
			}



			npa_t finish_signal_host_npa = {host_coord_x, host_coord_y, tg->kernel->finish_signal_addr};
			eva_t finish_signal_host_eva;
			error = hb_mc_npa_to_eva(device->eva_id, &finish_signal_host_npa, &finish_signal_host_eva); /* tile will write to this address when it finishes executing the kernel */
			if (error != HB_MC_SUCCESS) {
				fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_npa_to_eva(): failed to get finish_signal_host_eva from finish_signal_host_npa.\n");
				return HB_MC_FAIL;
			}

			error = hb_mc_write_tile_reg(device->fd, device->eva_id, &(device->grid->tiles[tile_id]), SIGNAL_REG, finish_signal_host_eva); 
			if (error != HB_MC_SUCCESS) {
				fprintf(stderr, "hb_mc_tile_group_allocate_tile_group() --> hb_mc_write_tile_reg(): failed to write finish_signal_addr %d to device for tile group %d.\n", finish_signal_host_eva, tg->id);
				return HB_MC_FAIL;
			}

			error = hb_mc_write_tile_reg(device->fd, device->eva_id, &(device->grid->tiles[tile_id]), KERNEL_REG, kernel_eva); /* write kernel EVA to tile group */
			if (error != HB_MC_SUCCESS) {
				fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_write_tile_reg(): failed to write kernel eva %d to device for tile group %d.\n", kernel_eva, tg->id);
				return HB_MC_FAIL; 
			}
		}
	} 

	return HB_MC_SUCCESS;
}



int hb_mc_wait_for_packet(device_t *device, hb_mc_request_packet_t *packet) {

	while (1) {	
		hb_mc_request_packet_t recv;
		hb_mc_fifo_receive(device->fd, HB_MC_MMIO_FIFO_TO_HOST, (hb_mc_packet_t *) &recv); 
		fprintf(stderr, "Received Packet: src (%d,%d), dst (%d,%d), addr: 0x%x, data: 0x%x.\n", recv.x_src, recv.y_src, recv.x_dst, recv.y_dst, recv.addr, recv.data); 
		if (hb_mc_request_packet_equals(&recv, packet) == HB_MC_SUCCESS)
			break; 
	}
	return HB_MC_SUCCESS;
}




int hb_mc_tile_group_sync (device_t *device, tile_group_t *tg) { 
	if (hb_mc_check_device(device->fd) != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_tile_group_sync) --> hb_mc_check_device(): failed to verify device.\n"); 
		return HB_MC_FAIL;
	}

	hb_mc_request_packet_t finish;
	hb_mc_format_response_packet (&finish, tg->kernel->finish_signal_addr, 0x1 /* TODO: magic number */, tg->origin_x, tg->origin_y, HB_MC_PACKET_OP_REMOTE_STORE);	
	fprintf(stderr, "Requested Packet: src (%d,%d), dst (%d,%d), addr: 0x%x, data: 0x%x.\n", finish.x_src, finish.y_src, finish.x_dst, finish.y_dst, finish.addr, finish.data); 
	hb_mc_wait_for_packet(device, &finish); 
	return HB_MC_SUCCESS;	
}	

/* 
 * @param[in] De-allocates all tiles in tile group, and resets their tile-group id and origin in the device book keeping.
 * @param[in] device device pointer.
 * @parma[in] tg tile group pointer.
 * @return HB_MC_SUCCESS if tile group is launched successfully and HB_MC_FAIL otherwise.
 * */
int hb_mc_tile_group_deallocate(device_t *device, tile_group_t *tg) {
	if (hb_mc_check_device(device->fd) != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_tile_group_launch() --> hb_mc_check_device(): failed to verify device.\n"); 
		return HB_MC_FAIL;
	}

	int tile_id;
	for (int x = tg->origin_x; x < tg->origin_x + tg->dim_x; x++){
		for (int y = tg->origin_y; y < tg->origin_y + tg->dim_y; y++){
			tile_id = (y - device->grid->origin_y) * device->grid->dim_x + (x - device->grid->origin_x);
			device->grid->tiles[tile_id].origin_x = device->grid->origin_x;
			device->grid->tiles[tile_id].origin_y = device->grid->origin_y;
			device->grid->tiles[tile_id].tile_group_id = -1;
			device->grid->tiles[tile_id].free = 1;
		}
	}
	// TODO delete tile group.
	printf("%dx%d tile group %d de-allocated at origin (%d,%d).\n", tg->dim_x, tg->dim_y, tg->id, tg->origin_x, tg->origin_y);

	return HB_MC_SUCCESS;
}





/*
 * Initializes Manycore tiles so that they may run kernels.
 * @param fd userspace file descriptor, which must be obtained from hb_mc_host_init.
 * @param eva_id specifies what the EVA-NPA mapping is.
 * @param tiles an array of tile_t structs to initialize.
 * @param num_tiles the number of tiles to initialize.
 * @return HB_MC_SUCCESS on success and HB_MC_FAIL on failure. 
 */
int hb_mc_device_init (device_t *device, eva_id_t eva_id, char *elf, uint8_t dim_x, uint8_t dim_y, uint8_t origin_x, uint8_t origin_y) {
	
	int error = hb_mc_fifo_init(fd); 
	if (error != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_device_init() --> hb_mc_host_init(): failed to initialize host.\n");
		return HB_MC_FAIL;
	}
	
	if (eva_id != 0) {
		return HB_MC_FAIL; /* eva_id not supported */
	} 
	device->eva_id = eva_id;
	device->elf = elf; 

	error = hb_mc_grid_init(device, dim_x, dim_y, origin_x, origin_y);
	if (error != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_device_init() --> hb_mc_grid_init(): failed to initialize grid.\n");
		return HB_MC_FAIL;
	}

	uint32_t num_tiles = device->grid->dim_x * device->grid->dim_y; 	


	for (int i = 0; i < num_tiles; i++) { /* initialize tiles */
		hb_mc_freeze(device->fd, device->grid->tiles[i].x, device->grid->tiles[i].y);
		hb_mc_tile_set_origin(device->fd, device->grid->tiles[i].x, device->grid->tiles[i].y, device->grid->tiles[i].origin_x, device->grid->tiles[i].origin_y);
	}


	/* load the elf into each tile */
	uint8_t x_list[num_tiles], y_list[num_tiles];	
	hb_mc_get_x(device->grid->tiles, &x_list[0], num_tiles);
	hb_mc_get_y(device->grid->tiles, &y_list[0], num_tiles); 
	hb_mc_load_binary(device->fd, device->elf, &x_list[0], &y_list[0], num_tiles);
	/* create a memory manager object */
	if (hb_mc_create_memory_manager(device->eva_id, device->elf) != HB_MC_SUCCESS)
		return HB_MC_FAIL;
  	
	/* unfreeze the tile group */
	for (int i = 0; i < num_tiles; i++) {
		error = hb_mc_write_tile_reg(device->fd, device->eva_id, &(device->grid->tiles[i]), KERNEL_REG, 0x1); /* initialize the kernel register */
		if (error != HB_MC_SUCCESS)
			return HB_MC_FAIL;
		hb_mc_unfreeze_dep(device->fd, device->grid->tiles[i].x, device->grid->tiles[i].y);
	}
	return HB_MC_SUCCESS;
}

/*!
 * Initializes Manycore tiles so that they may run kernels.
 * @param fd userspace file descriptor
 * @param eva_id specifies what the EVA-NPA mapping is.
 * @param tiles an array of tile_t structs to initialize.
 * @param num_tiles the number of tiles to initialize.
 * @return HB_MC_SUCCESS on success and HB_MC_FAIL on failure. 
 */
int hb_mc_device_finish (device_t *device) {

	if (device->eva_id != 0) {
		fprintf(stderr, "hb_mc_device_finish(): error: eva_id not supported.\n"); 
		return HB_MC_FAIL;
	} 

	if (!mem_manager[device->eva_id])
		return HB_MC_SUCCESS; /* there is no memory manager to deinitialize */	
	delete(mem_manager[device->eva_id]);
	
	delete(mem_manager[eva_id]);
	
	for (int i = 0; i < num_tiles; i++) { /* freeze tiles */
		hb_mc_tile_freeze_dep(fd, tiles[i].x, tiles[i].y);
	}

	int error = hb_mc_fifo_finish(fd);
	if (error != HB_MC_SUCCESS) {
		fprintf(stderr, "hb_mc_device_finish() --> hb_mc_host_finish(): failed to terminate host.\n");
		return HB_MC_FAIL;
	}

	return HB_MC_SUCCESS;
}

/*!
 * This function is for testing hb_mc_device_init() only. 
 */
void _hb_mc_get_mem_manager_info(eva_id_t eva_id, uint32_t *start, uint32_t *size) {
	if (!mem_manager[eva_id]) {
		printf("_hb_mc_get_mem_manager_info(): mem manager not initialized.\n");
		return;
	}
	*start = mem_manager[eva_id]->start();
	*size =mem_manager[eva_id]->size();
}

/*!
 * allocates memory in Manycore
 *@param device pointer to the device.
 *@param size in bytes.
 *@param eva returned EVA address. Set to 0 on failure.
 *@return HB_MC_SUCCESS on success and HB_MC_FAIL on failure. This function can fail if eva_id is invalid or of the memory manager corresponding to eva_id has not been initialized.
 */
int hb_mc_device_malloc (device_t *device, uint32_t size, /*out*/ eva_t *eva) {
        *eva = 0;
	if (device->eva_id != 0) {
		fprintf(stderr, "hb_mc_device_malloc(): invalid EVA ID %d.\n", device->eva_id);
		return HB_MC_FAIL; 
	}
	else if (!mem_manager[device->eva_id]) {
		fprintf(stderr, "hb_mc_device_malloc(): error: memory manager not initialized.\n");
		return HB_MC_FAIL; 
	}

	eva_t result = mem_manager[device->eva_id]->alloc(size);
	if (result == awsbwhal::MemoryManager::mNull) {
		fprintf(stderr, "hb_mc_device_malloc(): failed to allocated memory.\n");	
		return HB_MC_FAIL; 
	}
        *eva = result;
	return HB_MC_SUCCESS;
}

/*!
 * frees Hammerblade Manycore memory.
 *@param device pointer to the device.
 *@param eva address to free.
 *@return HB_MC_SUCCESS on success and HB_MC_FAIL on failure. This function can fail if eva_id is invalid or of the memory manager corresponding to eva_id has not been initialized.
 */
int hb_mc_device_free (device_t *device, eva_t eva) {
	if (device->eva_id != 0) {
		fprintf(stderr, "hb_mc_device_free(): invalid EVA ID %d.\n", device->eva_id); 
		return HB_MC_FAIL; 
	}
	else if (!mem_manager[device->eva_id]) {
		fprintf(stderr, "hb_mc_device_free(): error: memory manager not initialized.\n");
		return HB_MC_FAIL; 
	}

	mem_manager[device->eva_id]->free(eva);
	return HB_MC_SUCCESS;
}

/*
 * caller must ensure eva_id is valid. */
static int hb_mc_cpy_to_eva (uint8_t fd, eva_id_t eva_id, eva_t dst, uint32_t *src) {
	npa_t npa;	
	int error = hb_mc_eva_to_npa_deprecated(eva_id, dst, &npa);
	if (error != HB_MC_SUCCESS) {
		return HB_MC_FAIL; /* could not convert EVA to an NPA */
	}
	error = hb_mc_copy_to_epa (fd, npa.x, npa.y, npa.epa, src, 1 /* 1 word */);
	if (error != HB_MC_SUCCESS) {
		return HB_MC_FAIL; /* could not send data to Manycore */
	}
	return HB_MC_SUCCESS;
}

/*
 * caller must esure eva_id is valid. 
 * */
static int hb_mc_cpy_from_eva (uint8_t fd, eva_id_t eva_id, hb_mc_response_packet_t *dest, eva_t src) {
	npa_t npa;	
	int error = hb_mc_eva_to_npa_deprecated(eva_id, src, &npa);
	if (error != HB_MC_SUCCESS) {
		return HB_MC_FAIL; /* could not convert EVA to an NPA */
	}
	error = hb_mc_copy_from_epa (fd, dest, npa.x, npa.y, npa.epa, 1 /* 1 word */);
	if (error != HB_MC_SUCCESS) {
		return HB_MC_FAIL; /* could not send data to Manycore */
	}
	return HB_MC_SUCCESS;
}

int hb_mc_device_memcpy (device_t *device, void *dst, const void *src, uint32_t count, enum hb_mc_memcpy_kind kind) {
	if (device->eva_id != 0) {
		fprintf(stderr, "hb_mc_device_memcpy(): invalid EVA ID %d.\n", device->eva_id);
		return HB_MC_FAIL; 
	}

	else if (kind == hb_mc_memcpy_to_device) { /* copy to Manycore */
		eva_t dst_eva = (eva_t) reinterpret_cast<uintptr_t>(dst);
		for (int i = 0; i < count; i += sizeof(uint32_t)) { /* copy one word at a time */
			char *src_word = (char *) src + i;
			int error = hb_mc_cpy_to_eva(device->fd, device->eva_id, dst_eva + i, (uint32_t *) (src_word)); 		
			if (error != HB_MC_SUCCESS) {
				fprintf(stderr, "hb_mc_device_memcpy() --> hb_mc_cpy_to_eva(): failed to copy to device.\n");
				return HB_MC_FAIL; 
			}
		}
		return HB_MC_SUCCESS;	
	}
	
	else if (kind == hb_mc_memcpy_to_host) { /* copy to Host */
		eva_t src_eva = (eva_t) reinterpret_cast<uintptr_t>(src);
		for (int i = 0; i < count; i += sizeof(uint32_t)) { /* copy one word at a time */
                        // read in a packet
                        hb_mc_response_packet_t dst_packet;
			int error = hb_mc_cpy_from_eva(device->fd, device->eva_id, &dst_packet, src_eva + i);
			if (error != HB_MC_SUCCESS) {
				fprintf(stderr, "hb_mc_device_memcpy() --> hb_mc_cpy_from_eva(): failed to copy to host.\n");
				return HB_MC_FAIL; 
			}

                        // copy the word into caller dst buffer
                        uint32_t *dst_w = (uint32_t*)dst;
                        dst_w[i/sizeof(uint32_t)] = hb_mc_response_packet_get_data(&dst_packet);
		}
		return HB_MC_SUCCESS;	
	}
	else {
		fprintf(stderr, "hb_mc_device_memcpy(): invalid copy type. Copy type can be one of hb_mc_memcpy_to_device or hb_mc_memcpy_to_host.\n");
		return HB_MC_FAIL; 
		}
}

void hb_mc_cuda_sync (uint8_t fd, tile_t *tile) {
	uint8_t host_x = hb_mc_get_manycore_dimension_x() -1;
	uint8_t host_y = 0;
	hb_mc_packet_op_t op = HB_MC_PACKET_OP_REMOTE_STORE;
	hb_mc_packet_mask_t mask = HB_MC_PACKET_REQUEST_MASK_WORD; 
	hb_mc_request_packet_t finish = {host_x, host_y, tile->x, tile->y, 0x1 /* data */, mask, op, FINISH_ADDRESS, {0, 0}};
	hb_mc_device_sync(fd, &finish);
} 

int hb_mc_device_launch (device_t *device, char *kernel, uint32_t argc, uint32_t argv[], char *elf, tile_t tiles[], uint32_t num_tiles) {
	eva_t args_eva;
        int error = hb_mc_device_malloc (device, argc * sizeof(uint32_t), &args_eva); /* allocate device memory for arguments */
        if (error != HB_MC_SUCCESS)
            return HB_MC_FAIL;
	error = hb_mc_device_memcpy(device, reinterpret_cast<void *>(args_eva), (void *) &argv[0], argc * sizeof(uint32_t), hb_mc_memcpy_to_device); /* transfer the arguments to dram */
	if (error != HB_MC_SUCCESS)
		return HB_MC_FAIL;
	
	eva_t kernel_eva; 
	error = symbol_to_eva(elf, kernel, &kernel_eva); /* get EVA of kernel */
	if (error != HB_MC_SUCCESS)
		return HB_MC_FAIL;
	
	for (int i = 0; i < num_tiles; i++) {
		error = hb_mc_write_tile_reg(device->fd, device->eva_id, &tiles[i], ARGC_REG, argc); /* write argc to tile */
		if (error != HB_MC_SUCCESS)
			return HB_MC_FAIL; 
		
		error = hb_mc_write_tile_reg(device->fd, device->eva_id, &tiles[i], ARGV_REG, args_eva); /* write EVA of arguments to tile group */
		if (error != HB_MC_SUCCESS)
			return HB_MC_FAIL; 


		npa_t host_npa = {(uint32_t) hb_mc_get_manycore_dimension_x() - 1, 0, FINISH_ADDRESS};
		eva_t host_eva;
		error = hb_mc_npa_to_eva_deprecated(device->eva_id, &host_npa, &host_eva); /* tile will write to this address when it finishes executing the kernel */
		if (error != HB_MC_SUCCESS)
			return HB_MC_FAIL;
		error = hb_mc_write_tile_reg(device->fd, device->eva_id, &tiles[i], SIGNAL_REG, host_eva); 
		if (error != HB_MC_SUCCESS)
			return HB_MC_FAIL;

		error = hb_mc_write_tile_reg(device->fd, device->eva_id, &tiles[i], KERNEL_REG, kernel_eva); /* write kernel EVA to tile group */
		if (error != HB_MC_SUCCESS)
			return HB_MC_FAIL; 
	} 

	return HB_MC_SUCCESS;
}


/*!
 * creates a tile group with a specified origin
 * @param[out] tiles an array of tiles that will be set in row-order. This should be allocated by the caller
 * @param[out] the number of tiles in the tile group
 * @param[in] num_tiles_x the number of columns in the tile group
 * @param[in] num_tiles_y the number of rows in the tile group
 * @param[in] origin_x the x coordinate of the tile group's origin
 * @param[in] origin_y the y coordinate of the tile group's origin 
 * */
void create_tile_group(tile_t tiles[], uint8_t num_tiles_x, uint8_t num_tiles_y, uint8_t origin_x, uint8_t origin_y) {
	/* create the tile group */
	for (uint8_t i = 0; i < num_tiles_y; i++) {
		for (uint8_t j = 0; j < num_tiles_x; j++) {
			uint32_t index = i * num_tiles_x + j;
			tiles[index].x = j + origin_x; 
			tiles[index].y = i + origin_y;
			tiles[index].origin_x = origin_x;
			tiles[index].origin_y = origin_y;
		}
	}
}
