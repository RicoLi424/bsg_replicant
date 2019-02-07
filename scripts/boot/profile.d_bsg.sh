echo "Initializing AWS FPGA Environment from /etc/profile.d/profile.d_bsg.sh"
echo "This will take a few seconds..."

echo "sudo rm -f /tmp/sdk_root_env.exp "
sudo rm -f /tmp/sdk_root_env.exp

echo ". $AWS_FPGA_REPO_DIR/hdk_setup.sh > /tmp/hdk_setup.log"
. $AWS_FPGA_REPO_DIR/hdk_setup.sh > /tmp/hdk_setup.log

echo ". $AWS_FPGA_REPO_DIR/sdk_setup.sh > /tmp/sdk_setup.log"
. $AWS_FPGA_REPO_DIR/sdk_setup.sh > /tmp/sdk_setup.log

echo "Initialization done"
