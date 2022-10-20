/*
Copyright (c) 2022 TOYOTA MOTOR CORPORATION
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted (subject to the limitations in the disclaimer
below) provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its contributors may be used
  to endorse or promote products derived from this software without specific
  prior written permission.

NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED BY THIS
LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
DAMAGE.
*/
#include "../src/hsrb_robot_hardware/hsrb_hw_joint.hpp"
#include "../src/hsrb_robot_hardware/joint_communication.hpp"

#include "mocks.hpp"

namespace {

const boost::system::error_code kSuccess =
    boost::system::error_code(boost::system::errc::success, boost::system::system_category());
const boost::system::error_code kTimeOut =
    boost::system::error_code(boost::system::errc::timed_out, boost::system::system_category());

template<typename TYPE>
double ExtractValue(const std::vector<TYPE>& handles, const std::string& name) {
  for (const auto& handle : handles) {
    if (handle.get_interface_name() == name) {
      return handle.get_value();
    }
  }
  return 0.0;
}

template<typename TYPE>
double ExtractValue(const std::vector<TYPE>& handles, const std::string& joint_name, const std::string& if_name) {
  for (const auto& handle : handles) {
    if (handle.get_name() == joint_name && handle.get_interface_name() == if_name) {
      return handle.get_value();
    }
  }
  return 0.0;
}


void SetCommandValue(const std::string& name, double value,
                     std::vector<hardware_interface::CommandInterface>& handles) {
  for (auto& handle : handles) {
    if (handle.get_interface_name() == name) {
      handle.set_value(value);
      return;
    }
  }
}

}  // namespace

namespace hsrb_robot_hardware {

class ActiveJointTest : public ::testing::Test {
 protected:
  void SetUp() override;

  JointParameters params_;
  std::shared_ptr<JointCommunicationMock> comm_;

  std::shared_ptr<ActiveJoint> joint_;
};

void ActiveJointTest::SetUp() {
  params_ = JointParameters();
  params_.joint_name = "test_joint";
  params_.reduction = 2.0;
  params_.position_offset = 0.3;
  params_.default_drive_mode = 5.0;
  params_.velocity_limit = 0.2;
  params_.motor_to_joint_gear_ratio = 4.0;
  params_.torque_constant = 5.0;

  comm_ = std::make_shared<JointCommunicationMock>();
  joint_ = std::make_shared<ActiveJoint>(comm_, params_);
}

TEST_F(ActiveJointTest, StartNormal) {
  EXPECT_CALL(*comm_, ResetAlarm()).Times(1);
  EXPECT_CALL(*comm_, ResetDriveMode(5)).Times(1);
  EXPECT_CALL(*comm_, GetCurrentPosition(kMotorAxis, ::testing::_))
      .Times(1).WillRepeatedly(::testing::SetArgReferee<1>(3.0));
  EXPECT_CALL(*comm_, ResetVelocityLimit(0.2)).Times(1);

  joint_->start();
  EXPECT_EQ(ExtractValue(joint_->export_command_interfaces(), hardware_interface::HW_IF_POSITION), 3.0 / 2.0 + 0.3);
}

TEST_F(ActiveJointTest, StartWheel) {
  params_.control_type = "Wheel";
  joint_ = std::make_shared<ActiveJoint>(comm_, params_);

  EXPECT_CALL(*comm_, ResetAlarm()).Times(1);
  EXPECT_CALL(*comm_, ResetDriveMode(5)).Times(1);
  EXPECT_CALL(*comm_, ResetVelocityLimit(0.2)).Times(1);
  EXPECT_CALL(*comm_, GetCurrentPosition(kMotorAxis, ::testing::_)).Times(1);
  EXPECT_CALL(*comm_, ResetPosition()).Times(1);

  joint_->start();
}

TEST_F(ActiveJointTest, StartWithDriveMode) {
  std::vector<std::tuple<uint8_t, JointAxis, uint32_t>> test_cases = {
      {3, kMotorAxis, 0}, {4, kMotorAxis, 1}, {5, kMotorAxis, 1},
      {6, kMotorAxis, 0}, {7, kOutputAxis, 1}, {8, kOutputAxis, 1}};

  EXPECT_CALL(*comm_, ResetAlarm()).Times(test_cases.size());
  EXPECT_CALL(*comm_, ResetVelocityLimit(0.2)).Times(test_cases.size());

  for (const auto& test_case : test_cases) {
    params_.default_drive_mode = std::get<0>(test_case);
    joint_ = std::make_shared<ActiveJoint>(comm_, params_);

    EXPECT_CALL(*comm_, ResetDriveMode(std::get<0>(test_case))).Times(1);
    EXPECT_CALL(*comm_, GetCurrentPosition(std::get<1>(test_case), ::testing::_)).Times(std::get<2>(test_case));
    joint_->start();
  }
}

TEST_F(ActiveJointTest, ReadNormal) {
  JointCommunication::ReadValues values;
  values.motor_outaxis_position = 1.0;
  values.motor_outaxis_velocity = 2.0;
  values.joint_calc_outaxis_correct_position = 3.0;
  values.current = 4.0;

  const double position_motor = 1.0 / 2.0 + 0.3;
  const double position_outaxis = 3.0 / 2.0 + 0.3;
  std::vector<std::tuple<uint8_t, double>> test_cases = {{3, position_motor},   {4, position_motor},
                                                         {5, position_motor},   {6, position_motor},
                                                         {7, position_outaxis}, {8, position_outaxis}};

  for (const auto& test_case : test_cases) {
    values.drive_mode = std::get<0>(test_case);

    EXPECT_CALL(*comm_, Read(::testing::_))
        .Times(1)
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<0>(values), ::testing::Return(kSuccess)));

    joint_->read();
    EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), hardware_interface::HW_IF_POSITION),
              std::get<1>(test_case));
    EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), hardware_interface::HW_IF_VELOCITY), 1.0);
    EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), hardware_interface::HW_IF_EFFORT), 10.0);
    EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "current_drive_mode"), values.drive_mode);
  }
}

TEST_F(ActiveJointTest, ReadError) {
  JointCommunication::ReadValues values;
  values.motor_outaxis_position = 1.0;
  values.motor_outaxis_velocity = 2.0;
  values.joint_calc_outaxis_correct_position = 3.0;
  values.current = 4.0;
  values.drive_mode = 5;

  EXPECT_CALL(*comm_, Read(::testing::_))
      .Times(1)
      .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<0>(values), ::testing::Return(kTimeOut)));

  joint_->read();
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), hardware_interface::HW_IF_POSITION), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), hardware_interface::HW_IF_VELOCITY), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), hardware_interface::HW_IF_EFFORT), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "current_drive_mode"), 0.0);
}

TEST_F(ActiveJointTest, WritePosition) {
  auto command_handles = joint_->export_command_interfaces();
  SetCommandValue(hardware_interface::HW_IF_POSITION, 1.0, command_handles);

  std::vector<uint8_t> drive_modes = {4, 5, 7, 8, 9};
  for (auto drive_mode : drive_modes) {
    JointCommunication::ReadValues values;
    values.drive_mode = drive_mode;

    EXPECT_CALL(*comm_, Read(::testing::_))
        .Times(1)
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<0>(values), ::testing::Return(kSuccess)));
    joint_->read();

    EXPECT_CALL(*comm_, WriteCommandPosition((1.0 - 0.3) * 2.0)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
    EXPECT_CALL(*comm_, WriteCommandVelocity(::testing::_)).Times(0);
    joint_->write();
  }
}

TEST_F(ActiveJointTest, WriteVelocity) {
  std::vector<uint8_t> drive_modes = {3, 6};
  for (auto drive_mode : drive_modes) {
    JointCommunication::ReadValues values;
    values.drive_mode = drive_mode;

    EXPECT_CALL(*comm_, Read(::testing::_))
        .Times(1)
        .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<0>(values), ::testing::Return(kSuccess)));
    joint_->read();

    auto command_handles = joint_->export_command_interfaces();
    SetCommandValue(hardware_interface::HW_IF_VELOCITY, 1.0, command_handles);

    EXPECT_CALL(*comm_, WriteCommandPosition(::testing::_)).Times(0);
    EXPECT_CALL(*comm_, WriteCommandVelocity(1.0 * 2.0)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
    joint_->write();
  }
}

TEST_F(ActiveJointTest, WriteDriveMode) {
  auto command_handles = joint_->export_command_interfaces();
  SetCommandValue("command_drive_mode", 5.0, command_handles);

  EXPECT_CALL(*comm_, SetDriveMode(5)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
  joint_->write();

  EXPECT_EQ(ExtractValue(command_handles, "command_drive_mode"), -1.0);
}

TEST_F(ActiveJointTest, ResetCommandVelocity) {
  auto command_handles = joint_->export_command_interfaces();
  SetCommandValue(hardware_interface::HW_IF_VELOCITY, 1.0, command_handles);

  JointCommunication::ReadValues values;
  values.drive_mode = 3;

  EXPECT_CALL(*comm_, Read(::testing::_))
      .Times(1)
      .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<0>(values), ::testing::Return(kSuccess)));
  joint_->read();

  EXPECT_CALL(*comm_, WriteCommandVelocity(0.0)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
  joint_->write();
}

class GripperActiveJointTest : public ::testing::Test {
 protected:
  void SetUp() override;

  void SetDriveMode(uint32_t value, bool grasping_flag = false);

  JointParameters params_;
  GripperJointParameters gripper_params_;
  std::shared_ptr<GripperCommunicationMock> comm_;

  std::shared_ptr<GripperActiveJoint> joint_;
};

void GripperActiveJointTest::SetUp() {
  params_ = JointParameters();
  params_.joint_name = "test_joint";
  params_.reduction = 2.0;
  params_.position_offset = 0.3;
  params_.default_drive_mode = 21.0;
  params_.velocity_limit = 0.2;
  params_.motor_to_joint_gear_ratio = 4.0;
  params_.torque_constant = 5.0;

  gripper_params_.left_spring_joint = "left_joint";
  gripper_params_.right_spring_joint = "right_joint";

  comm_ = std::make_shared<GripperCommunicationMock>();
  joint_ = std::make_shared<GripperActiveJoint>(comm_, params_, gripper_params_);
}

void GripperActiveJointTest::SetDriveMode(uint32_t value, bool grasping_flag) {
  GripperCommunication::GripperValues values;
  values.drive_mode = value;
  values.hand_grasping_flag = grasping_flag ? 1.0 : -1.0;

  EXPECT_CALL(*comm_, Read(::testing::_))
      .Times(1)
      .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<0>(values), ::testing::Return(kSuccess)));
  joint_->read();
}

TEST_F(GripperActiveJointTest, ReadNormal) {
  GripperCommunication::GripperValues values;
  values.current = 10.0;
  values.drive_mode = 31.0;
  values.hand_motor_position = 2.0;
  values.hand_left_position = 3.0;
  values.hand_right_position = 4.0;
  values.hand_motor_velocity = 5.0;
  values.hand_left_velocity = 6.0;
  values.hand_right_velocity = 7.0;
  values.hand_left_force = 8.0;
  values.hand_right_force = 9.0;
  values.hand_grasping_flag = 1.0 + 1e-7;

  EXPECT_CALL(*comm_, Read(::testing::_))
      .Times(1)
      .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<0>(values), ::testing::Return(kSuccess)));

  joint_->read();
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "test_joint", hardware_interface::HW_IF_POSITION),
            2.0 / 2.0 + 0.3);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "test_joint", hardware_interface::HW_IF_VELOCITY),
            5.0 / 2.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "test_joint", hardware_interface::HW_IF_EFFORT),
            5.0 * 4.0 + 5.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "test_joint", "current_drive_mode"), 31.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "test_joint", "current_grasping_flag"), 1.0);

  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "left_joint", hardware_interface::HW_IF_POSITION),
            3.0 - 2.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "left_joint", hardware_interface::HW_IF_VELOCITY), 6.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "left_joint", hardware_interface::HW_IF_EFFORT), 8.0);

  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "right_joint", hardware_interface::HW_IF_POSITION),
            4.0 - 2.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "right_joint", hardware_interface::HW_IF_VELOCITY), 7.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "right_joint", hardware_interface::HW_IF_EFFORT), 9.0);
}

TEST_F(GripperActiveJointTest, ReadError) {
  GripperCommunication::GripperValues values;
  values.current = 10.0;
  values.drive_mode = 31.0;
  values.hand_motor_position = 2.0;
  values.hand_left_position = 3.0;
  values.hand_right_position = 4.0;
  values.hand_motor_velocity = 5.0;
  values.hand_left_velocity = 6.0;
  values.hand_right_velocity = 7.0;
  values.hand_left_force = 8.0;
  values.hand_right_force = 9.0;
  values.hand_grasping_flag = 1.0 + 1e-7;

  EXPECT_CALL(*comm_, Read(::testing::_))
      .Times(1)
      .WillRepeatedly(::testing::DoAll(::testing::SetArgReferee<0>(values), ::testing::Return(kTimeOut)));

  joint_->read();
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "test_joint", hardware_interface::HW_IF_POSITION), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "test_joint", hardware_interface::HW_IF_VELOCITY), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "test_joint", hardware_interface::HW_IF_EFFORT), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "test_joint", "current_drive_mode"), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "test_joint", "current_grasping_flag"), -1.0);

  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "left_joint", hardware_interface::HW_IF_POSITION), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "left_joint", hardware_interface::HW_IF_VELOCITY), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "left_joint", hardware_interface::HW_IF_EFFORT), 0.0);

  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "right_joint", hardware_interface::HW_IF_POSITION), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "right_joint", hardware_interface::HW_IF_VELOCITY), 0.0);
  EXPECT_EQ(ExtractValue(joint_->export_state_interfaces(), "right_joint", hardware_interface::HW_IF_EFFORT), 0.0);
}

TEST_F(GripperActiveJointTest, WritePosition) {
  SetDriveMode(21);

  auto command_handles = joint_->export_command_interfaces();
  SetCommandValue(hardware_interface::HW_IF_POSITION, 1.0, command_handles);

  EXPECT_CALL(*comm_, WriteCommandPosition((1.0 - 0.3) * 2.0)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
  EXPECT_CALL(*comm_, WriteCommandForce(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteCommandEffort(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteGraspingFlag(::testing::_)).Times(0);
  joint_->write();
}

TEST_F(GripperActiveJointTest, WriteForce) {
  SetDriveMode(22);

  auto command_handles = joint_->export_command_interfaces();
  SetCommandValue("command_force", 1.0, command_handles);

  EXPECT_CALL(*comm_, WriteCommandPosition(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteCommandForce(1.0)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
  EXPECT_CALL(*comm_, WriteCommandEffort(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteGraspingFlag(::testing::_)).Times(0);
  joint_->write();
}

TEST_F(GripperActiveJointTest, WriteGrasp) {
  auto command_handles = joint_->export_command_interfaces();
  SetCommandValue("effort", 2.0, command_handles);

  SetCommandValue("command_grasping_flag", 1.0, command_handles);
  SetDriveMode(20, false);

  EXPECT_CALL(*comm_, WriteCommandPosition(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteCommandForce(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteCommandEffort(2.0)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
  EXPECT_CALL(*comm_, WriteGraspingFlag(true)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
  joint_->write();

  SetCommandValue("command_grasping_flag", -1.0, command_handles);
  SetDriveMode(20, false);

  EXPECT_CALL(*comm_, WriteCommandPosition(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteCommandForce(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteCommandEffort(2.0)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
  EXPECT_CALL(*comm_, WriteGraspingFlag(::testing::_)).Times(0);
  joint_->write();

  SetCommandValue("command_grasping_flag", 1.0, command_handles);
  SetDriveMode(20, true);

  EXPECT_CALL(*comm_, WriteCommandPosition(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteCommandForce(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteCommandEffort(2.0)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
  EXPECT_CALL(*comm_, WriteGraspingFlag(::testing::_)).Times(0);
  joint_->write();

  SetCommandValue("command_grasping_flag", 1.0, command_handles);
  SetDriveMode(20, false);

  EXPECT_CALL(*comm_, WriteCommandPosition(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteCommandForce(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteCommandEffort(::testing::_)).Times(0);
  EXPECT_CALL(*comm_, WriteGraspingFlag(true)).Times(1).WillRepeatedly(::testing::Return(kTimeOut));
  joint_->write();
}

TEST_F(GripperActiveJointTest, WriteDriveMode) {
  auto command_handles = joint_->export_command_interfaces();
  SetCommandValue("command_drive_mode", 23.0, command_handles);

  EXPECT_CALL(*comm_, SetDriveMode(23)).Times(1).WillRepeatedly(::testing::Return(kSuccess));
  joint_->write();

  EXPECT_EQ(ExtractValue(command_handles, "command_drive_mode"), -1.0);
}

}  // namespace hsrb_robot_hardware

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
