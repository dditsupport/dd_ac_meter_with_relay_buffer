-- phpMyAdmin SQL Dump
-- version 5.2.2
-- https://www.phpmyadmin.net/
--
-- Host: localhost:3306
-- Generation Time: Jul 03, 2026 at 04:29 PM
-- Server version: 10.11.18-MariaDB-cll-lve
-- PHP Version: 8.4.22

SET SQL_MODE = "NO_AUTO_VALUE_ON_ZERO";
START TRANSACTION;
SET time_zone = "+00:00";


/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8mb4 */;

--
-- Database: `gtvpheud_workpulse`
--

-- --------------------------------------------------------

--
-- Table structure for table `annotation_comments`
--

CREATE TABLE `annotation_comments` (
  `id` int(10) UNSIGNED NOT NULL,
  `annotation_id` int(10) UNSIGNED NOT NULL,
  `employee_code` varchar(50) CHARACTER SET utf8mb3 COLLATE utf8mb3_unicode_ci NOT NULL,
  `comment_text` text NOT NULL,
  `created_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `annotation_images`
--

CREATE TABLE `annotation_images` (
  `id` int(10) UNSIGNED NOT NULL,
  `location_id` int(11) NOT NULL,
  `image_date` date NOT NULL,
  `original_name` varchar(255) NOT NULL,
  `stored_name` varchar(255) NOT NULL,
  `mime_type` varchar(100) NOT NULL,
  `size_bytes` int(10) UNSIGNED NOT NULL,
  `caption` varchar(500) DEFAULT NULL,
  `question_id` int(10) UNSIGNED DEFAULT NULL,
  `store_manager_code` varchar(20) DEFAULT NULL,
  `uploaded_by` varchar(50) CHARACTER SET utf8mb3 COLLATE utf8mb3_unicode_ci NOT NULL,
  `uploaded_at` datetime NOT NULL DEFAULT current_timestamp(),
  `deleted_at` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `attendance_logs`
--

CREATE TABLE `attendance_logs` (
  `id` int(11) NOT NULL,
  `employee_code` varchar(50) NOT NULL,
  `device_serial` varchar(100) NOT NULL,
  `device_type` enum('MFS500','FM220') NOT NULL,
  `location_id` int(11) NOT NULL,
  `punch_type` enum('IN','OUT') NOT NULL,
  `punch_method` enum('fingerprint','otp','manual','auto_close') NOT NULL DEFAULT 'fingerprint',
  `match_score` int(11) DEFAULT 0,
  `punch_time` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audits`
--

CREATE TABLE `audits` (
  `id` int(11) NOT NULL,
  `audit_number` varchar(20) DEFAULT NULL,
  `template_id` int(11) NOT NULL,
  `location_id` int(11) NOT NULL,
  `auditor_code` varchar(20) NOT NULL,
  `store_manager_code` varchar(20) DEFAULT NULL,
  `store_executive_code` varchar(20) DEFAULT NULL,
  `operation_code` varchar(20) DEFAULT NULL,
  `approver_code` varchar(20) DEFAULT NULL,
  `management_code` varchar(20) DEFAULT NULL,
  `status` enum('draft','submitted','manager_review','operation_review','approver_review','management_review','approved','sent_back') NOT NULL DEFAULT 'draft',
  `total_score` decimal(6,2) DEFAULT NULL,
  `audit_date` date NOT NULL,
  `submitted_at` datetime DEFAULT NULL,
  `manager_reviewed_at` datetime DEFAULT NULL,
  `operation_reviewed_at` datetime DEFAULT NULL,
  `approver_reviewed_at` datetime DEFAULT NULL,
  `approved_at` datetime DEFAULT NULL,
  `management_approved_at` datetime DEFAULT NULL,
  `created_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audit_categories`
--

CREATE TABLE `audit_categories` (
  `id` int(11) NOT NULL,
  `template_id` int(11) NOT NULL,
  `name` varchar(191) NOT NULL,
  `weightage` decimal(6,2) NOT NULL,
  `sort_order` int(11) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audit_category_weights`
--

CREATE TABLE `audit_category_weights` (
  `audit_id` int(11) NOT NULL,
  `category_id` int(11) NOT NULL,
  `actual_weightage` decimal(6,2) NOT NULL DEFAULT 0.00,
  `category_name` varchar(191) DEFAULT NULL,
  `modified_weightage` decimal(6,2) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audit_history`
--

CREATE TABLE `audit_history` (
  `id` int(11) NOT NULL,
  `audit_id` int(11) NOT NULL,
  `action` varchar(30) NOT NULL,
  `by_code` varchar(20) NOT NULL,
  `remark` text DEFAULT NULL,
  `at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audit_image_pins`
--

CREATE TABLE `audit_image_pins` (
  `id` int(10) UNSIGNED NOT NULL,
  `attachment_id` int(11) NOT NULL,
  `pin_number` int(11) NOT NULL,
  `x_percent` decimal(5,2) NOT NULL,
  `y_percent` decimal(5,2) NOT NULL,
  `status` enum('open','resolved') NOT NULL DEFAULT 'open',
  `created_by` varchar(50) NOT NULL,
  `created_at` datetime NOT NULL DEFAULT current_timestamp(),
  `resolved_by` varchar(50) DEFAULT NULL,
  `resolved_at` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audit_image_pin_comments`
--

CREATE TABLE `audit_image_pin_comments` (
  `id` int(10) UNSIGNED NOT NULL,
  `pin_id` int(10) UNSIGNED NOT NULL,
  `employee_code` varchar(50) NOT NULL,
  `comment_text` text NOT NULL,
  `created_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audit_parameters`
--

CREATE TABLE `audit_parameters` (
  `id` int(11) NOT NULL,
  `category_id` int(11) NOT NULL,
  `parameter_text` text NOT NULL,
  `type` enum('rating','value','boolean') NOT NULL DEFAULT 'rating',
  `max_value` decimal(10,2) DEFAULT NULL,
  `score_weightage` decimal(6,2) NOT NULL,
  `sort_order` int(11) NOT NULL DEFAULT 0,
  `is_active` tinyint(1) NOT NULL DEFAULT 1
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audit_responses`
--

CREATE TABLE `audit_responses` (
  `id` int(11) NOT NULL,
  `audit_id` int(11) NOT NULL,
  `parameter_id` int(11) NOT NULL,
  `category_id` int(11) DEFAULT NULL,
  `category_name` varchar(191) DEFAULT NULL,
  `parameter_text` text DEFAULT NULL,
  `parameter_type` enum('rating','value','boolean') DEFAULT NULL,
  `parameter_max_value` decimal(10,2) DEFAULT NULL,
  `value_entered` decimal(10,2) DEFAULT NULL,
  `obtain_score` decimal(6,2) DEFAULT NULL,
  `actual_weightage` decimal(6,2) NOT NULL DEFAULT 0.00,
  `modified_weightage` decimal(6,2) NOT NULL,
  `auditor_remark` text DEFAULT NULL,
  `approver_remark` text DEFAULT NULL,
  `management_remark` varchar(2000) DEFAULT NULL,
  `store_manager_remark` varchar(2000) DEFAULT NULL,
  `operation_remark` varchar(2000) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audit_response_attachments`
--

CREATE TABLE `audit_response_attachments` (
  `id` int(11) NOT NULL,
  `response_id` int(11) NOT NULL,
  `filename` varchar(255) NOT NULL,
  `stored_name` varchar(255) NOT NULL,
  `mime_type` varchar(80) NOT NULL,
  `file_size` int(11) NOT NULL,
  `uploaded_by` varchar(20) NOT NULL,
  `uploaded_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audit_templates`
--

CREATE TABLE `audit_templates` (
  `id` int(11) NOT NULL,
  `name` varchar(150) NOT NULL,
  `is_active` tinyint(1) NOT NULL DEFAULT 1,
  `created_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `audit_view_logs`
--

CREATE TABLE `audit_view_logs` (
  `id` int(10) UNSIGNED NOT NULL,
  `audit_id` int(11) NOT NULL,
  `employee_code` varchar(20) NOT NULL,
  `view_type` enum('page','attachment') NOT NULL DEFAULT 'page',
  `attachment_id` int(11) DEFAULT NULL,
  `ip_address` varchar(45) DEFAULT NULL,
  `user_agent` varchar(500) DEFAULT NULL,
  `viewed_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `chk_daily_responses`
--

CREATE TABLE `chk_daily_responses` (
  `id` int(10) UNSIGNED NOT NULL,
  `location_id` int(11) NOT NULL,
  `item_id` int(10) UNSIGNED NOT NULL,
  `employee_code` varchar(20) NOT NULL,
  `log_date` date NOT NULL,
  `response_value` varchar(255) DEFAULT NULL,
  `submitted_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `chk_items`
--

CREATE TABLE `chk_items` (
  `id` int(10) UNSIGNED NOT NULL,
  `task_description` varchar(500) NOT NULL,
  `section_name` varchar(100) DEFAULT NULL,
  `input_type` enum('yes_no','time','text','number') NOT NULL DEFAULT 'yes_no',
  `is_active` tinyint(1) NOT NULL DEFAULT 1,
  `created_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `chk_response_attachments`
--

CREATE TABLE `chk_response_attachments` (
  `id` int(10) UNSIGNED NOT NULL,
  `response_id` int(10) UNSIGNED NOT NULL,
  `filename` varchar(255) NOT NULL,
  `stored_name` varchar(255) NOT NULL,
  `mime_type` varchar(80) NOT NULL,
  `file_size` int(11) NOT NULL,
  `uploaded_by` varchar(20) NOT NULL,
  `uploaded_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `chk_validations`
--

CREATE TABLE `chk_validations` (
  `id` int(10) UNSIGNED NOT NULL,
  `location_id` int(11) NOT NULL,
  `item_id` int(10) UNSIGNED NOT NULL,
  `log_date` date NOT NULL,
  `status` enum('done','not_done') NOT NULL,
  `remarks` varchar(500) DEFAULT NULL,
  `validated_by` varchar(20) NOT NULL,
  `validated_at` datetime NOT NULL DEFAULT current_timestamp(),
  `updated_at` datetime DEFAULT current_timestamp() ON UPDATE current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `departments`
--

CREATE TABLE `departments` (
  `id` int(11) NOT NULL,
  `department_name` varchar(100) NOT NULL,
  `is_active` tinyint(1) DEFAULT 1,
  `created_at` timestamp NOT NULL DEFAULT current_timestamp(),
  `email1` varchar(150) DEFAULT NULL,
  `email2` varchar(150) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;

-- --------------------------------------------------------

--
-- Table structure for table `devices`
--

CREATE TABLE `devices` (
  `device_id` int(11) NOT NULL,
  `device_serial` varchar(100) NOT NULL,
  `device_name` varchar(100) DEFAULT NULL,
  `device_type` enum('MFS500','FM220') NOT NULL DEFAULT 'MFS500',
  `location_id` int(11) NOT NULL,
  `is_active` tinyint(4) DEFAULT 1,
  `registered_at` timestamp NOT NULL DEFAULT current_timestamp(),
  `updated_at` timestamp NOT NULL DEFAULT current_timestamp() ON UPDATE current_timestamp(),
  `app_version` varchar(20) DEFAULT NULL COMMENT 'Last reported app version, e.g. 2.1.0',
  `version_updated_at` datetime DEFAULT NULL COMMENT 'Timestamp when app_version was last changed (NULL = never reported)'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;

-- --------------------------------------------------------

--
-- Table structure for table `employees`
--

CREATE TABLE `employees` (
  `id` int(11) NOT NULL,
  `employee_code` varchar(50) NOT NULL,
  `full_name` varchar(100) NOT NULL,
  `department_id` int(11) DEFAULT NULL,
  `staff_type` enum('retail','ho','factory') NOT NULL DEFAULT 'retail',
  `role_id` int(10) UNSIGNED DEFAULT NULL,
  `phone` varchar(20) DEFAULT NULL,
  `email` varchar(100) DEFAULT NULL,
  `portal_password` varchar(100) DEFAULT NULL,
  `join_date` date DEFAULT NULL,
  `enrollment_status` enum('pending_enrollment','partial','active','inactive') DEFAULT 'pending_enrollment',
  `template_mfs500_base64` mediumtext DEFAULT NULL,
  `template_fm220_base64` mediumtext DEFAULT NULL,
  `match_threshold` int(11) DEFAULT NULL,
  `otp_channel` enum('none','email','sms') NOT NULL DEFAULT 'none',
  `otp_device_bypass` tinyint(4) NOT NULL DEFAULT 0,
  `is_active` tinyint(4) DEFAULT 1,
  `deactivated_at` datetime DEFAULT NULL,
  `deactivation_reason` varchar(255) DEFAULT NULL,
  `created_at` timestamp NOT NULL DEFAULT current_timestamp(),
  `updated_at` timestamp NULL DEFAULT NULL ON UPDATE current_timestamp(),
  `location_id` int(11) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;

-- --------------------------------------------------------

--
-- Table structure for table `employee_location_logs`
--

CREATE TABLE `employee_location_logs` (
  `id` int(10) UNSIGNED NOT NULL,
  `employee_code` varchar(20) NOT NULL,
  `old_location_id` int(11) DEFAULT NULL,
  `new_location_id` int(11) NOT NULL,
  `changed_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `failed_punch_logs`
--

CREATE TABLE `failed_punch_logs` (
  `id` int(11) NOT NULL,
  `employee_code` varchar(50) DEFAULT NULL,
  `device_serial` varchar(100) NOT NULL,
  `device_type` enum('MFS500','FM220') NOT NULL,
  `location_id` int(11) DEFAULT NULL,
  `punch_type` enum('IN','OUT') DEFAULT NULL,
  `punch_method` enum('fingerprint','otp') NOT NULL DEFAULT 'fingerprint',
  `match_score` int(11) DEFAULT NULL,
  `threshold_used` int(11) DEFAULT NULL,
  `fail_reason` varchar(100) DEFAULT NULL,
  `app_version` varchar(50) DEFAULT NULL,
  `attempted_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;

-- --------------------------------------------------------

--
-- Table structure for table `image_annotations`
--

CREATE TABLE `image_annotations` (
  `id` int(10) UNSIGNED NOT NULL,
  `image_id` int(10) UNSIGNED NOT NULL,
  `pin_number` int(11) NOT NULL,
  `x_percent` decimal(5,2) NOT NULL,
  `y_percent` decimal(5,2) NOT NULL,
  `status` enum('open','resolved') NOT NULL DEFAULT 'open',
  `created_by` varchar(50) CHARACTER SET utf8mb3 COLLATE utf8mb3_unicode_ci NOT NULL,
  `created_at` datetime NOT NULL DEFAULT current_timestamp(),
  `resolved_by` varchar(50) CHARACTER SET utf8mb3 COLLATE utf8mb3_unicode_ci DEFAULT NULL,
  `resolved_at` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `issues`
--

CREATE TABLE `issues` (
  `id` int(10) UNSIGNED NOT NULL,
  `summary` varchar(300) NOT NULL,
  `description` text DEFAULT NULL,
  `status` enum('waiting_for_customer','assigned_to_concerned','in_progress','resolved','closed') NOT NULL DEFAULT 'assigned_to_concerned',
  `priority` enum('low','medium','high','critical') NOT NULL DEFAULT 'medium',
  `category_id` int(10) UNSIGNED DEFAULT NULL,
  `location_id` int(11) NOT NULL,
  `reporter_code` varchar(20) NOT NULL,
  `created_at` datetime DEFAULT current_timestamp(),
  `updated_at` datetime DEFAULT current_timestamp() ON UPDATE current_timestamp(),
  `resolved_at` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `issue_attachments`
--

CREATE TABLE `issue_attachments` (
  `id` int(10) UNSIGNED NOT NULL,
  `issue_id` int(10) UNSIGNED NOT NULL,
  `comment_id` int(10) UNSIGNED DEFAULT NULL,
  `filename` varchar(255) NOT NULL,
  `stored_name` varchar(255) NOT NULL,
  `mime_type` varchar(100) NOT NULL,
  `file_size` int(10) UNSIGNED NOT NULL,
  `uploaded_by` varchar(20) NOT NULL,
  `created_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `issue_categories`
--

CREATE TABLE `issue_categories` (
  `id` int(10) UNSIGNED NOT NULL,
  `category_group` enum('advance_maintenance','hr_issue','service_type','incident') NOT NULL,
  `category_name` varchar(150) NOT NULL,
  `is_active` tinyint(1) NOT NULL DEFAULT 1,
  `sort_order` int(11) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `issue_category_roles`
--

CREATE TABLE `issue_category_roles` (
  `category_id` int(10) UNSIGNED NOT NULL,
  `department_id` int(11) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `issue_comments`
--

CREATE TABLE `issue_comments` (
  `id` int(10) UNSIGNED NOT NULL,
  `issue_id` int(10) UNSIGNED NOT NULL,
  `author_code` varchar(20) NOT NULL,
  `body` text NOT NULL,
  `created_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `issue_participants`
--

CREATE TABLE `issue_participants` (
  `id` int(10) UNSIGNED NOT NULL,
  `issue_id` int(10) UNSIGNED NOT NULL,
  `department_id` int(11) NOT NULL,
  `added_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `issue_status_logs`
--

CREATE TABLE `issue_status_logs` (
  `id` int(10) UNSIGNED NOT NULL,
  `issue_id` int(10) UNSIGNED NOT NULL,
  `old_status` varchar(50) NOT NULL,
  `new_status` varchar(50) NOT NULL,
  `changed_by` varchar(20) NOT NULL,
  `changed_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=latin1 COLLATE=latin1_swedish_ci;

-- --------------------------------------------------------

--
-- Table structure for table `locations`
--

CREATE TABLE `locations` (
  `location_id` int(11) NOT NULL,
  `location_name` varchar(100) NOT NULL,
  `contact_email` varchar(100) DEFAULT NULL,
  `contact_phone` varchar(15) DEFAULT NULL,
  `is_active` tinyint(4) DEFAULT 1,
  `created_at` timestamp NOT NULL DEFAULT current_timestamp(),
  `location_code` varchar(20) DEFAULT NULL,
  `address` text DEFAULT NULL,
  `latitude` decimal(10,7) DEFAULT NULL,
  `longitude` decimal(10,7) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;

-- --------------------------------------------------------

--
-- Table structure for table `location_managers`
--

CREATE TABLE `location_managers` (
  `location_id` int(11) NOT NULL,
  `store_manager_code` varchar(20) NOT NULL,
  `updated_by` varchar(20) DEFAULT NULL,
  `updated_at` datetime DEFAULT current_timestamp() ON UPDATE current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `offer_coupons`
--

CREATE TABLE `offer_coupons` (
  `id` int(10) UNSIGNED NOT NULL,
  `Name` varchar(255) DEFAULT NULL,
  `Mobile` varchar(15) DEFAULT NULL,
  `Bill_Amount` decimal(10,2) DEFAULT NULL,
  `Outlet` varchar(255) DEFAULT NULL,
  `Approver` varchar(255) DEFAULT NULL,
  `Offer` varchar(100) NOT NULL,
  `Remark` varchar(500) DEFAULT NULL,
  `Coupon` varchar(100) NOT NULL,
  `is_redeemed` tinyint(1) NOT NULL DEFAULT 0,
  `IPAddress` varchar(45) DEFAULT NULL,
  `datestamp` date DEFAULT NULL,
  `timestamp` time DEFAULT NULL,
  `employee_code` varchar(20) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `otp_logs`
--

CREATE TABLE `otp_logs` (
  `id` int(11) NOT NULL,
  `employee_code` varchar(50) NOT NULL,
  `otp_hash` varchar(64) NOT NULL,
  `channel` enum('email','sms') NOT NULL,
  `sent_at` datetime DEFAULT current_timestamp(),
  `expires_at` datetime NOT NULL,
  `used_at` datetime DEFAULT NULL,
  `is_used` tinyint(4) DEFAULT 0,
  `verify_attempts` int(11) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;

-- --------------------------------------------------------

--
-- Table structure for table `policies`
--

CREATE TABLE `policies` (
  `id` int(10) UNSIGNED NOT NULL,
  `title` varchar(200) NOT NULL,
  `category` enum('hr','it','store_management') NOT NULL,
  `is_active` tinyint(1) NOT NULL DEFAULT 1,
  `created_by` varchar(50) NOT NULL,
  `created_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `policy_audience`
--

CREATE TABLE `policy_audience` (
  `id` int(10) UNSIGNED NOT NULL,
  `policy_version_id` int(10) UNSIGNED NOT NULL,
  `department_id` int(11) DEFAULT NULL,
  `location_id` int(11) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `policy_consents`
--

CREATE TABLE `policy_consents` (
  `id` int(10) UNSIGNED NOT NULL,
  `policy_version_id` int(10) UNSIGNED NOT NULL,
  `employee_code` varchar(50) CHARACTER SET utf8mb3 COLLATE utf8mb3_unicode_ci NOT NULL,
  `pdf_sha256_at_consent` char(64) NOT NULL,
  `consented_at` datetime NOT NULL DEFAULT current_timestamp(),
  `ip_address` varchar(45) DEFAULT NULL,
  `user_agent` varchar(500) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `policy_versions`
--

CREATE TABLE `policy_versions` (
  `id` int(10) UNSIGNED NOT NULL,
  `policy_id` int(10) UNSIGNED NOT NULL,
  `version_label` varchar(40) NOT NULL,
  `effective_date` date NOT NULL,
  `pdf_original_name` varchar(255) NOT NULL,
  `pdf_stored_name` varchar(255) NOT NULL,
  `pdf_sha256` char(64) NOT NULL,
  `pdf_size_bytes` int(10) UNSIGNED NOT NULL,
  `is_major_update` tinyint(1) NOT NULL DEFAULT 0,
  `allow_download` tinyint(1) NOT NULL DEFAULT 0,
  `otp_required` tinyint(1) NOT NULL DEFAULT 1,
  `what_changed` text DEFAULT NULL,
  `grace_days` int(11) NOT NULL DEFAULT 7,
  `published_by` varchar(50) NOT NULL,
  `published_at` datetime NOT NULL DEFAULT current_timestamp(),
  `superseded_at` datetime DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `price_list`
--

CREATE TABLE `price_list` (
  `id` int(10) UNSIGNED NOT NULL,
  `item_code` varchar(64) NOT NULL,
  `item_name` varchar(255) NOT NULL,
  `swiggy_name` varchar(255) DEFAULT NULL,
  `zomato_name` varchar(255) DEFAULT NULL,
  `online_3x_price` decimal(10,2) NOT NULL DEFAULT 0.00,
  `online_4x_price` decimal(10,2) NOT NULL DEFAULT 0.00,
  `online_5x_price` decimal(10,2) NOT NULL DEFAULT 0.00,
  `online_6x_price` decimal(10,2) NOT NULL DEFAULT 0.00,
  `tax_pct` decimal(5,2) NOT NULL DEFAULT 0.00,
  `created_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `price_variations`
--

CREATE TABLE `price_variations` (
  `id` int(10) UNSIGNED NOT NULL,
  `location_id` int(10) UNSIGNED NOT NULL,
  `location_name` varchar(255) NOT NULL,
  `partner` enum('swiggy','zomato') NOT NULL,
  `order_id` varchar(64) NOT NULL,
  `order_date` date DEFAULT NULL,
  `bill_subtotal` decimal(10,2) NOT NULL DEFAULT 0.00,
  `discount_amount` decimal(10,2) NOT NULL DEFAULT 0.00,
  `taxes` decimal(10,2) NOT NULL DEFAULT 0.00,
  `net_received` decimal(10,2) NOT NULL DEFAULT 0.00,
  `expected_3x_amount` decimal(10,2) NOT NULL DEFAULT 0.00,
  `expected_4x_amount` decimal(10,2) NOT NULL DEFAULT 0.00,
  `expected_5x_amount` decimal(10,2) NOT NULL DEFAULT 0.00,
  `expected_6x_amount` decimal(10,2) NOT NULL DEFAULT 0.00,
  `variance_3x` decimal(10,2) NOT NULL DEFAULT 0.00,
  `variance_4x` decimal(10,2) NOT NULL DEFAULT 0.00,
  `variance_5x` decimal(10,2) NOT NULL DEFAULT 0.00,
  `variance_6x` decimal(10,2) NOT NULL DEFAULT 0.00,
  `variance_3x_pct` decimal(7,2) NOT NULL DEFAULT 0.00,
  `variance_4x_pct` decimal(7,2) NOT NULL DEFAULT 0.00,
  `variance_5x_pct` decimal(7,2) NOT NULL DEFAULT 0.00,
  `variance_6x_pct` decimal(7,2) NOT NULL DEFAULT 0.00,
  `remarks` text DEFAULT NULL,
  `status` enum('pending','confirmed','approved','rejected') NOT NULL DEFAULT 'pending',
  `submitted_by` varchar(50) NOT NULL,
  `submitted_at` datetime DEFAULT current_timestamp(),
  `decided_by` varchar(50) DEFAULT NULL,
  `decided_at` datetime DEFAULT NULL,
  `decision_remarks` text DEFAULT NULL,
  `confirmed_by` varchar(20) DEFAULT NULL,
  `confirmed_at` datetime DEFAULT NULL,
  `confirm_remarks` varchar(500) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `price_variation_attachments`
--

CREATE TABLE `price_variation_attachments` (
  `id` int(10) UNSIGNED NOT NULL,
  `variation_id` int(10) UNSIGNED NOT NULL,
  `original_name` varchar(255) NOT NULL,
  `stored_name` varchar(255) NOT NULL,
  `mime_type` varchar(100) DEFAULT NULL,
  `size_bytes` int(10) UNSIGNED DEFAULT NULL,
  `uploaded_by` varchar(50) NOT NULL,
  `uploaded_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `price_variation_items`
--

CREATE TABLE `price_variation_items` (
  `id` int(10) UNSIGNED NOT NULL,
  `variation_id` int(10) UNSIGNED NOT NULL,
  `price_list_id` int(10) UNSIGNED DEFAULT NULL,
  `item_code` varchar(64) NOT NULL,
  `item_name` varchar(255) NOT NULL,
  `online_3x_price` decimal(10,2) NOT NULL DEFAULT 0.00,
  `online_4x_price` decimal(10,2) NOT NULL DEFAULT 0.00,
  `online_5x_price` decimal(10,2) NOT NULL DEFAULT 0.00,
  `online_6x_price` decimal(10,2) NOT NULL DEFAULT 0.00,
  `tax_pct` decimal(5,2) NOT NULL DEFAULT 0.00,
  `quantity` int(11) NOT NULL,
  `partner_rate` decimal(10,2) DEFAULT NULL,
  `expected_3x` decimal(10,2) NOT NULL DEFAULT 0.00,
  `expected_4x` decimal(10,2) NOT NULL DEFAULT 0.00,
  `expected_5x` decimal(10,2) NOT NULL DEFAULT 0.00,
  `expected_6x` decimal(10,2) NOT NULL DEFAULT 0.00
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `product_shelf_life`
--

CREATE TABLE `product_shelf_life` (
  `id` int(10) UNSIGNED NOT NULL,
  `item_group` varchar(100) NOT NULL,
  `item_code` varchar(50) NOT NULL,
  `item_name` varchar(255) NOT NULL,
  `shelf_life_days` int(11) NOT NULL,
  `basic` decimal(10,2) DEFAULT NULL,
  `tax` decimal(5,2) DEFAULT NULL,
  `mrp` decimal(10,2) DEFAULT NULL,
  `description` text DEFAULT NULL,
  `image` varchar(255) DEFAULT NULL,
  `created_at` datetime DEFAULT current_timestamp(),
  `updated_at` datetime DEFAULT current_timestamp() ON UPDATE current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `punch_requests`
--

CREATE TABLE `punch_requests` (
  `id` int(10) UNSIGNED NOT NULL,
  `employee_code` varchar(50) NOT NULL,
  `location_id` int(11) NOT NULL,
  `punch_date` date NOT NULL,
  `punch_time` time NOT NULL,
  `punch_type` enum('IN','OUT') NOT NULL,
  `reason` varchar(500) NOT NULL,
  `attachment_name` varchar(255) DEFAULT NULL,
  `attachment_stored` varchar(255) DEFAULT NULL,
  `status` enum('pending','approved','rejected') NOT NULL DEFAULT 'pending',
  `reviewed_by` varchar(50) DEFAULT NULL,
  `reviewed_at` datetime DEFAULT NULL,
  `review_note` varchar(500) DEFAULT NULL,
  `created_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `roles`
--

CREATE TABLE `roles` (
  `id` int(10) UNSIGNED NOT NULL,
  `role_name` varchar(80) NOT NULL,
  `is_active` tinyint(1) NOT NULL DEFAULT 1,
  `created_at` datetime DEFAULT current_timestamp(),
  `txn_dashboard` tinyint(1) NOT NULL DEFAULT 0,
  `txn_employees` tinyint(1) NOT NULL DEFAULT 0,
  `txn_departments` tinyint(1) NOT NULL DEFAULT 0,
  `txn_locations` tinyint(1) NOT NULL DEFAULT 0,
  `txn_attendance` tinyint(1) NOT NULL DEFAULT 0,
  `txn_approve_punches` tinyint(1) NOT NULL DEFAULT 0,
  `txn_failed_punches` tinyint(1) NOT NULL DEFAULT 0,
  `txn_issues` tinyint(1) NOT NULL DEFAULT 0,
  `txn_create_issue` tinyint(1) NOT NULL DEFAULT 0,
  `txn_edit_issue` tinyint(1) NOT NULL DEFAULT 0,
  `txn_issue_summary` tinyint(1) NOT NULL DEFAULT 0,
  `txn_issue_comments` tinyint(1) NOT NULL DEFAULT 0,
  `txn_checklist` tinyint(1) NOT NULL DEFAULT 0,
  `txn_manage_tasks` tinyint(1) NOT NULL DEFAULT 0,
  `txn_checklist_report` tinyint(1) NOT NULL DEFAULT 0,
  `txn_checklist_audit` tinyint(1) NOT NULL DEFAULT 0,
  `txn_offer` tinyint(1) NOT NULL DEFAULT 0,
  `txn_coupon_redeemed` tinyint(1) NOT NULL DEFAULT 0,
  `txn_devices` tinyint(1) NOT NULL DEFAULT 0,
  `txn_manage_categories` tinyint(1) NOT NULL DEFAULT 0,
  `txn_generate_coupons` tinyint(1) NOT NULL DEFAULT 0,
  `txn_manage_passwords` tinyint(1) NOT NULL DEFAULT 0,
  `txn_settings` tinyint(1) NOT NULL DEFAULT 0,
  `txn_generate_vouchers` tinyint(1) NOT NULL DEFAULT 0,
  `txn_outlet_directory` tinyint(1) NOT NULL DEFAULT 0,
  `txn_shelf_life` tinyint(1) NOT NULL DEFAULT 0,
  `txn_shelf_life_upload` tinyint(1) NOT NULL DEFAULT 0,
  `txn_store_hours` tinyint(1) NOT NULL DEFAULT 0,
  `txn_dependencies` tinyint(1) NOT NULL DEFAULT 0,
  `txn_dept_roles` tinyint(1) NOT NULL DEFAULT 0,
  `txn_price_tags` tinyint(1) NOT NULL DEFAULT 0,
  `txn_audit_create` tinyint(1) NOT NULL DEFAULT 0,
  `txn_audit_approve` tinyint(1) NOT NULL DEFAULT 0,
  `txn_audit_operation` tinyint(1) NOT NULL DEFAULT 0,
  `txn_audit_management` tinyint(1) NOT NULL DEFAULT 0,
  `txn_audit_annotation_resolve` tinyint(1) NOT NULL DEFAULT 0,
  `txn_audit_admin` tinyint(1) NOT NULL DEFAULT 0,
  `txn_audit_view` tinyint(1) NOT NULL DEFAULT 0,
  `txn_violations_view` tinyint(1) NOT NULL DEFAULT 0,
  `txn_record_violation` tinyint(1) NOT NULL DEFAULT 0,
  `txn_reset_violation_counter` tinyint(1) NOT NULL DEFAULT 0,
  `txn_violation_admin` tinyint(1) NOT NULL DEFAULT 0,
  `txn_price_variation` tinyint(1) NOT NULL DEFAULT 0,
  `txn_price_variation_admin` tinyint(1) NOT NULL DEFAULT 0,
  `txn_transactions_report` tinyint(1) NOT NULL DEFAULT 0,
  `txn_audit_summary` tinyint(1) NOT NULL DEFAULT 0,
  `txn_price_variation_confirm` tinyint(1) NOT NULL DEFAULT 0,
  `txn_annotation_create` tinyint(1) NOT NULL DEFAULT 0,
  `txn_annotation_comment` tinyint(1) NOT NULL DEFAULT 0,
  `txn_annotation_resolve` tinyint(1) NOT NULL DEFAULT 0,
  `txn_sh_questions` tinyint(1) NOT NULL DEFAULT 0,
  `txn_policy_dashboard` tinyint(1) NOT NULL DEFAULT 0,
  `txn_policy_admin` tinyint(1) NOT NULL DEFAULT 0,
  `txn_checklist_validate` tinyint(1) NOT NULL DEFAULT 0,
  `txn_time_report` tinyint(1) NOT NULL DEFAULT 0,
  `txn_ticket_scheduler` tinyint(1) NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `sh_check_questions`
--

CREATE TABLE `sh_check_questions` (
  `id` int(10) UNSIGNED NOT NULL,
  `question_text` varchar(500) NOT NULL,
  `answer_type` enum('rating_1_5','yes_no') NOT NULL,
  `sort_order` int(11) NOT NULL DEFAULT 0,
  `is_active` tinyint(4) NOT NULL DEFAULT 1,
  `created_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `system_settings`
--

CREATE TABLE `system_settings` (
  `id` int(11) NOT NULL,
  `setting_key` varchar(100) NOT NULL,
  `setting_value` varchar(500) NOT NULL,
  `description` varchar(255) DEFAULT NULL,
  `updated_at` timestamp NOT NULL DEFAULT current_timestamp() ON UPDATE current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COLLATE=utf8mb3_unicode_ci;

-- --------------------------------------------------------

--
-- Table structure for table `ticket_schedules`
--

CREATE TABLE `ticket_schedules` (
  `id` int(10) UNSIGNED NOT NULL,
  `title` varchar(150) NOT NULL,
  `summary` varchar(300) NOT NULL,
  `description` text DEFAULT NULL,
  `priority` enum('low','medium','high','critical') NOT NULL DEFAULT 'medium',
  `category_id` int(10) UNSIGNED DEFAULT NULL,
  `location_id` int(11) NOT NULL,
  `event_date` date NOT NULL,
  `lead_days` int(11) NOT NULL DEFAULT 0,
  `recurrence` enum('once','daily','weekly','monthly','yearly') NOT NULL DEFAULT 'once',
  `recur_interval` int(11) NOT NULL DEFAULT 1,
  `is_active` tinyint(1) NOT NULL DEFAULT 1,
  `last_created_at` datetime DEFAULT NULL,
  `last_issue_id` int(10) UNSIGNED DEFAULT NULL,
  `created_by` varchar(20) NOT NULL,
  `created_at` datetime NOT NULL DEFAULT current_timestamp(),
  `updated_at` datetime DEFAULT current_timestamp() ON UPDATE current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `ticket_schedule_depts`
--

CREATE TABLE `ticket_schedule_depts` (
  `schedule_id` int(10) UNSIGNED NOT NULL,
  `department_id` int(11) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `time_entries`
--

CREATE TABLE `time_entries` (
  `id` int(10) UNSIGNED NOT NULL,
  `employee_code` varchar(20) NOT NULL,
  `issue_id` int(10) UNSIGNED DEFAULT NULL,
  `task_id` int(10) UNSIGNED DEFAULT NULL,
  `task_label` varchar(200) DEFAULT NULL,
  `entry_date` date NOT NULL,
  `minutes` int(10) UNSIGNED NOT NULL,
  `notes` text DEFAULT NULL,
  `created_at` datetime NOT NULL DEFAULT current_timestamp(),
  `updated_at` datetime DEFAULT current_timestamp() ON UPDATE current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `time_tasks`
--

CREATE TABLE `time_tasks` (
  `id` int(10) UNSIGNED NOT NULL,
  `employee_code` varchar(20) NOT NULL,
  `name` varchar(200) NOT NULL,
  `is_active` tinyint(1) NOT NULL DEFAULT 1,
  `created_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `transactions`
--

CREATE TABLE `transactions` (
  `id` int(10) UNSIGNED NOT NULL,
  `location_id` int(11) NOT NULL,
  `txn_date` date NOT NULL,
  `amount` decimal(12,2) NOT NULL,
  `remark` varchar(500) DEFAULT NULL,
  `original_name` varchar(255) NOT NULL,
  `stored_name` varchar(255) NOT NULL,
  `mime_type` varchar(100) DEFAULT NULL,
  `size_bytes` int(10) UNSIGNED DEFAULT NULL,
  `uploaded_by` varchar(50) NOT NULL,
  `uploaded_at` datetime DEFAULT current_timestamp(),
  `validated_at` datetime DEFAULT NULL,
  `validated_by` varchar(50) DEFAULT NULL,
  `invalidated_at` datetime DEFAULT NULL,
  `invalidated_by` varchar(50) DEFAULT NULL,
  `invalidate_reason` varchar(500) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `violations`
--

CREATE TABLE `violations` (
  `id` int(10) UNSIGNED NOT NULL,
  `category_id` int(10) UNSIGNED NOT NULL,
  `location_id` int(10) UNSIGNED NOT NULL,
  `employee_code` varchar(50) DEFAULT NULL,
  `recorded_by` varchar(50) NOT NULL,
  `counter_value` int(11) NOT NULL,
  `penalty_amount` decimal(10,2) NOT NULL DEFAULT 0.00,
  `action_type` varchar(40) NOT NULL DEFAULT 'penalty',
  `action_label` varchar(255) NOT NULL,
  `shortage_amount` decimal(10,2) DEFAULT NULL,
  `custom_amount` decimal(10,2) DEFAULT NULL,
  `notes` text DEFAULT NULL,
  `created_at` datetime DEFAULT current_timestamp(),
  `deleted_at` datetime DEFAULT NULL,
  `deleted_by` varchar(50) DEFAULT NULL,
  `delete_reason` varchar(500) DEFAULT NULL,
  `attachment_name` varchar(255) DEFAULT NULL,
  `attachment_stored` varchar(120) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `violation_categories`
--

CREATE TABLE `violation_categories` (
  `id` int(10) UNSIGNED NOT NULL,
  `name` varchar(120) NOT NULL,
  `slug` varchar(60) NOT NULL,
  `penalty_type` enum('fixed_tier','amount_based','escalating','custom_amount','workflow_only') NOT NULL DEFAULT 'fixed_tier',
  `corrective_action_text` text DEFAULT NULL,
  `needs_shortage_amount` tinyint(1) NOT NULL DEFAULT 0,
  `needs_custom_amount` tinyint(1) NOT NULL DEFAULT 0,
  `custom_amount_label` varchar(80) DEFAULT NULL,
  `sort_order` int(11) NOT NULL DEFAULT 0,
  `is_active` tinyint(1) NOT NULL DEFAULT 1,
  `created_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `violation_category_tiers`
--

CREATE TABLE `violation_category_tiers` (
  `id` int(10) UNSIGNED NOT NULL,
  `category_id` int(10) UNSIGNED NOT NULL,
  `tier_number` int(11) NOT NULL,
  `amount` decimal(10,2) DEFAULT NULL,
  `action_type` enum('penalty','intimation_letter','show_cause','warning','written_warning','workflow') NOT NULL DEFAULT 'penalty',
  `action_label` varchar(255) NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `violation_counter_resets`
--

CREATE TABLE `violation_counter_resets` (
  `id` int(10) UNSIGNED NOT NULL,
  `location_id` int(10) UNSIGNED NOT NULL,
  `category_id` int(10) UNSIGNED NOT NULL,
  `counter_value_before_reset` int(11) NOT NULL DEFAULT 0,
  `reset_by` varchar(50) NOT NULL,
  `reset_at` datetime DEFAULT current_timestamp(),
  `reason` varchar(500) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `violation_remarks`
--

CREATE TABLE `violation_remarks` (
  `id` int(10) UNSIGNED NOT NULL,
  `violation_id` int(10) UNSIGNED NOT NULL,
  `remark_by` varchar(50) NOT NULL,
  `remark_role` varchar(40) NOT NULL DEFAULT 'manager',
  `remark` text NOT NULL,
  `created_at` datetime DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

-- --------------------------------------------------------

--
-- Table structure for table `vouchers`
--

CREATE TABLE `vouchers` (
  `id` int(10) UNSIGNED NOT NULL,
  `code` varchar(10) NOT NULL,
  `batch_number` int(11) NOT NULL,
  `prefix` varchar(10) NOT NULL,
  `created_at` datetime NOT NULL DEFAULT current_timestamp()
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_general_ci;

--
-- Indexes for dumped tables
--

--
-- Indexes for table `annotation_comments`
--
ALTER TABLE `annotation_comments`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_ann_created` (`annotation_id`,`created_at`);

--
-- Indexes for table `annotation_images`
--
ALTER TABLE `annotation_images`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_loc_date` (`location_id`,`image_date`),
  ADD KEY `idx_uploaded_at` (`uploaded_at`),
  ADD KEY `idx_question_id` (`question_id`),
  ADD KEY `idx_store_manager_code` (`store_manager_code`);

--
-- Indexes for table `attendance_logs`
--
ALTER TABLE `attendance_logs`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_emp_time` (`employee_code`,`punch_time`),
  ADD KEY `idx_location_time` (`location_id`,`punch_time`),
  ADD KEY `idx_device_time` (`device_serial`,`punch_time`),
  ADD KEY `idx_att_punch_time` (`punch_time`);

--
-- Indexes for table `audits`
--
ALTER TABLE `audits`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `audit_number` (`audit_number`),
  ADD KEY `template_id` (`template_id`),
  ADD KEY `ix_aud_loc` (`location_id`),
  ADD KEY `ix_aud_auditor` (`auditor_code`),
  ADD KEY `ix_aud_status` (`status`),
  ADD KEY `ix_aud_date` (`audit_date`);

--
-- Indexes for table `audit_categories`
--
ALTER TABLE `audit_categories`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `ux_cat_tpl_name` (`template_id`,`name`);

--
-- Indexes for table `audit_category_weights`
--
ALTER TABLE `audit_category_weights`
  ADD PRIMARY KEY (`audit_id`,`category_id`),
  ADD KEY `category_id` (`category_id`);

--
-- Indexes for table `audit_history`
--
ALTER TABLE `audit_history`
  ADD PRIMARY KEY (`id`),
  ADD KEY `ix_aud_hist` (`audit_id`);

--
-- Indexes for table `audit_image_pins`
--
ALTER TABLE `audit_image_pins`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uniq_attachment_pin` (`attachment_id`,`pin_number`),
  ADD KEY `idx_attachment_status` (`attachment_id`,`status`);

--
-- Indexes for table `audit_image_pin_comments`
--
ALTER TABLE `audit_image_pin_comments`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_pin_id` (`pin_id`);

--
-- Indexes for table `audit_parameters`
--
ALTER TABLE `audit_parameters`
  ADD PRIMARY KEY (`id`),
  ADD KEY `category_id` (`category_id`);

--
-- Indexes for table `audit_responses`
--
ALTER TABLE `audit_responses`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `ux_audit_param` (`audit_id`,`parameter_id`),
  ADD KEY `parameter_id` (`parameter_id`),
  ADD KEY `ix_resp_cat` (`category_id`);

--
-- Indexes for table `audit_response_attachments`
--
ALTER TABLE `audit_response_attachments`
  ADD PRIMARY KEY (`id`),
  ADD KEY `response_id` (`response_id`);

--
-- Indexes for table `audit_templates`
--
ALTER TABLE `audit_templates`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `name` (`name`);

--
-- Indexes for table `audit_view_logs`
--
ALTER TABLE `audit_view_logs`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_audit_view_audit` (`audit_id`,`viewed_at`),
  ADD KEY `idx_audit_view_emp` (`employee_code`,`viewed_at`);

--
-- Indexes for table `chk_daily_responses`
--
ALTER TABLE `chk_daily_responses`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uq_response` (`location_id`,`item_id`,`employee_code`,`log_date`),
  ADD KEY `idx_location_date` (`location_id`,`log_date`),
  ADD KEY `idx_employee_code` (`employee_code`),
  ADD KEY `item_id` (`item_id`),
  ADD KEY `idx_checklist_loc_date` (`location_id`,`log_date`);

--
-- Indexes for table `chk_items`
--
ALTER TABLE `chk_items`
  ADD PRIMARY KEY (`id`);

--
-- Indexes for table `chk_response_attachments`
--
ALTER TABLE `chk_response_attachments`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_chk_att_response` (`response_id`);

--
-- Indexes for table `chk_validations`
--
ALTER TABLE `chk_validations`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uniq_val` (`location_id`,`item_id`,`log_date`),
  ADD KEY `idx_val_loc_date` (`location_id`,`log_date`);

--
-- Indexes for table `departments`
--
ALTER TABLE `departments`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uq_dept_name` (`department_name`);

--
-- Indexes for table `devices`
--
ALTER TABLE `devices`
  ADD PRIMARY KEY (`device_id`),
  ADD UNIQUE KEY `uq_device_serial` (`device_serial`),
  ADD KEY `fk_device_location` (`location_id`),
  ADD KEY `idx_devices_active` (`is_active`);

--
-- Indexes for table `employees`
--
ALTER TABLE `employees`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uq_employee_code` (`employee_code`),
  ADD KEY `idx_employee_code` (`employee_code`),
  ADD KEY `idx_emp_active` (`is_active`),
  ADD KEY `idx_emp_status_active` (`enrollment_status`,`is_active`);

--
-- Indexes for table `employee_location_logs`
--
ALTER TABLE `employee_location_logs`
  ADD PRIMARY KEY (`id`);

--
-- Indexes for table `failed_punch_logs`
--
ALTER TABLE `failed_punch_logs`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_failed_emp` (`employee_code`,`attempted_at`),
  ADD KEY `idx_failed_location` (`location_id`,`attempted_at`),
  ADD KEY `idx_failed_device` (`device_serial`,`attempted_at`);

--
-- Indexes for table `image_annotations`
--
ALTER TABLE `image_annotations`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uk_image_pin` (`image_id`,`pin_number`),
  ADD KEY `idx_status` (`status`);

--
-- Indexes for table `issues`
--
ALTER TABLE `issues`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_status` (`status`),
  ADD KEY `idx_reporter` (`reporter_code`),
  ADD KEY `idx_location` (`location_id`),
  ADD KEY `idx_category` (`category_id`),
  ADD KEY `idx_issue_status_assignee` (`status`),
  ADD KEY `idx_issues_created` (`created_at`);

--
-- Indexes for table `issue_attachments`
--
ALTER TABLE `issue_attachments`
  ADD PRIMARY KEY (`id`),
  ADD KEY `issue_id` (`issue_id`),
  ADD KEY `comment_id` (`comment_id`);

--
-- Indexes for table `issue_categories`
--
ALTER TABLE `issue_categories`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uq_group_name` (`category_group`,`category_name`);

--
-- Indexes for table `issue_category_roles`
--
ALTER TABLE `issue_category_roles`
  ADD PRIMARY KEY (`category_id`,`department_id`),
  ADD KEY `department_id` (`department_id`);

--
-- Indexes for table `issue_comments`
--
ALTER TABLE `issue_comments`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_ic_issue` (`issue_id`);

--
-- Indexes for table `issue_status_logs`
--
ALTER TABLE `issue_status_logs`
  ADD PRIMARY KEY (`id`),
  ADD KEY `issue_id` (`issue_id`);

--
-- Indexes for table `locations`
--
ALTER TABLE `locations`
  ADD PRIMARY KEY (`location_id`),
  ADD UNIQUE KEY `uq_location_name` (`location_name`);

--
-- Indexes for table `location_managers`
--
ALTER TABLE `location_managers`
  ADD PRIMARY KEY (`location_id`);

--
-- Indexes for table `offer_coupons`
--
ALTER TABLE `offer_coupons`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `Coupon` (`Coupon`),
  ADD KEY `idx_redeemed_offer` (`is_redeemed`,`Offer`),
  ADD KEY `idx_employee` (`employee_code`),
  ADD KEY `idx_date` (`datestamp`);

--
-- Indexes for table `otp_logs`
--
ALTER TABLE `otp_logs`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_otp_emp` (`employee_code`,`sent_at`),
  ADD KEY `idx_emp_active` (`employee_code`,`is_used`,`expires_at`);

--
-- Indexes for table `policies`
--
ALTER TABLE `policies`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_category_active` (`category`,`is_active`);

--
-- Indexes for table `policy_audience`
--
ALTER TABLE `policy_audience`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_pa_version` (`policy_version_id`);

--
-- Indexes for table `policy_consents`
--
ALTER TABLE `policy_consents`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uk_ver_emp` (`policy_version_id`,`employee_code`),
  ADD KEY `idx_pc_employee` (`employee_code`);

--
-- Indexes for table `policy_versions`
--
ALTER TABLE `policy_versions`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uk_policy_version` (`policy_id`,`version_label`),
  ADD KEY `idx_policy_published` (`policy_id`,`published_at`);

--
-- Indexes for table `price_list`
--
ALTER TABLE `price_list`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uniq_item_code` (`item_code`),
  ADD KEY `idx_item_name` (`item_name`),
  ADD KEY `idx_swiggy_name` (`swiggy_name`),
  ADD KEY `idx_zomato_name` (`zomato_name`);

--
-- Indexes for table `price_variations`
--
ALTER TABLE `price_variations`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_status` (`status`),
  ADD KEY `idx_location` (`location_id`),
  ADD KEY `idx_partner` (`partner`),
  ADD KEY `idx_order` (`order_id`),
  ADD KEY `idx_submitted_at` (`submitted_at`),
  ADD KEY `idx_submitted_by` (`submitted_by`);

--
-- Indexes for table `price_variation_attachments`
--
ALTER TABLE `price_variation_attachments`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_variation` (`variation_id`);

--
-- Indexes for table `price_variation_items`
--
ALTER TABLE `price_variation_items`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_variation` (`variation_id`);

--
-- Indexes for table `product_shelf_life`
--
ALTER TABLE `product_shelf_life`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uq_item_code` (`item_code`),
  ADD KEY `idx_item_group` (`item_group`);

--
-- Indexes for table `punch_requests`
--
ALTER TABLE `punch_requests`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_pr_employee` (`employee_code`),
  ADD KEY `idx_pr_status` (`status`),
  ADD KEY `idx_pr_date` (`punch_date`),
  ADD KEY `location_id` (`location_id`);

--
-- Indexes for table `roles`
--
ALTER TABLE `roles`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `role_name` (`role_name`);

--
-- Indexes for table `sh_check_questions`
--
ALTER TABLE `sh_check_questions`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_active_sort` (`is_active`,`sort_order`);

--
-- Indexes for table `system_settings`
--
ALTER TABLE `system_settings`
  ADD UNIQUE KEY `uq_setting_key` (`setting_key`);

--
-- Indexes for table `ticket_schedules`
--
ALTER TABLE `ticket_schedules`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_sched_due` (`is_active`,`event_date`);

--
-- Indexes for table `ticket_schedule_depts`
--
ALTER TABLE `ticket_schedule_depts`
  ADD PRIMARY KEY (`schedule_id`,`department_id`);

--
-- Indexes for table `time_entries`
--
ALTER TABLE `time_entries`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_te_emp_date` (`employee_code`,`entry_date`),
  ADD KEY `idx_te_issue` (`issue_id`);

--
-- Indexes for table `time_tasks`
--
ALTER TABLE `time_tasks`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_tt_emp` (`employee_code`,`is_active`);

--
-- Indexes for table `transactions`
--
ALTER TABLE `transactions`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_loc_date` (`location_id`,`txn_date`),
  ADD KEY `idx_date` (`txn_date`);

--
-- Indexes for table `violations`
--
ALTER TABLE `violations`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_loc_cat` (`location_id`,`category_id`,`deleted_at`),
  ADD KEY `idx_created` (`created_at`),
  ADD KEY `idx_category` (`category_id`),
  ADD KEY `idx_employee` (`employee_code`);

--
-- Indexes for table `violation_categories`
--
ALTER TABLE `violation_categories`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uniq_slug` (`slug`);

--
-- Indexes for table `violation_category_tiers`
--
ALTER TABLE `violation_category_tiers`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uniq_cat_tier` (`category_id`,`tier_number`),
  ADD KEY `idx_cat` (`category_id`);

--
-- Indexes for table `violation_counter_resets`
--
ALTER TABLE `violation_counter_resets`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_loc_cat` (`location_id`,`category_id`),
  ADD KEY `idx_reset_at` (`reset_at`);

--
-- Indexes for table `violation_remarks`
--
ALTER TABLE `violation_remarks`
  ADD PRIMARY KEY (`id`),
  ADD KEY `idx_violation` (`violation_id`);

--
-- Indexes for table `vouchers`
--
ALTER TABLE `vouchers`
  ADD PRIMARY KEY (`id`),
  ADD UNIQUE KEY `uq_voucher_code` (`code`);

--
-- AUTO_INCREMENT for dumped tables
--

--
-- AUTO_INCREMENT for table `annotation_comments`
--
ALTER TABLE `annotation_comments`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `annotation_images`
--
ALTER TABLE `annotation_images`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `attendance_logs`
--
ALTER TABLE `attendance_logs`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `audits`
--
ALTER TABLE `audits`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `audit_categories`
--
ALTER TABLE `audit_categories`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `audit_history`
--
ALTER TABLE `audit_history`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `audit_image_pins`
--
ALTER TABLE `audit_image_pins`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `audit_image_pin_comments`
--
ALTER TABLE `audit_image_pin_comments`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `audit_parameters`
--
ALTER TABLE `audit_parameters`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `audit_responses`
--
ALTER TABLE `audit_responses`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `audit_response_attachments`
--
ALTER TABLE `audit_response_attachments`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `audit_templates`
--
ALTER TABLE `audit_templates`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `audit_view_logs`
--
ALTER TABLE `audit_view_logs`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `chk_daily_responses`
--
ALTER TABLE `chk_daily_responses`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `chk_items`
--
ALTER TABLE `chk_items`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `chk_response_attachments`
--
ALTER TABLE `chk_response_attachments`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `chk_validations`
--
ALTER TABLE `chk_validations`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `departments`
--
ALTER TABLE `departments`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `devices`
--
ALTER TABLE `devices`
  MODIFY `device_id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `employees`
--
ALTER TABLE `employees`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `employee_location_logs`
--
ALTER TABLE `employee_location_logs`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `failed_punch_logs`
--
ALTER TABLE `failed_punch_logs`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `image_annotations`
--
ALTER TABLE `image_annotations`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `issues`
--
ALTER TABLE `issues`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `issue_attachments`
--
ALTER TABLE `issue_attachments`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `issue_categories`
--
ALTER TABLE `issue_categories`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `issue_comments`
--
ALTER TABLE `issue_comments`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `issue_status_logs`
--
ALTER TABLE `issue_status_logs`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `locations`
--
ALTER TABLE `locations`
  MODIFY `location_id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `offer_coupons`
--
ALTER TABLE `offer_coupons`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `otp_logs`
--
ALTER TABLE `otp_logs`
  MODIFY `id` int(11) NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `policies`
--
ALTER TABLE `policies`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `policy_audience`
--
ALTER TABLE `policy_audience`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `policy_consents`
--
ALTER TABLE `policy_consents`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `policy_versions`
--
ALTER TABLE `policy_versions`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `price_list`
--
ALTER TABLE `price_list`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `price_variations`
--
ALTER TABLE `price_variations`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `price_variation_attachments`
--
ALTER TABLE `price_variation_attachments`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `price_variation_items`
--
ALTER TABLE `price_variation_items`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `product_shelf_life`
--
ALTER TABLE `product_shelf_life`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `punch_requests`
--
ALTER TABLE `punch_requests`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `roles`
--
ALTER TABLE `roles`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `sh_check_questions`
--
ALTER TABLE `sh_check_questions`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `ticket_schedules`
--
ALTER TABLE `ticket_schedules`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `time_entries`
--
ALTER TABLE `time_entries`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `time_tasks`
--
ALTER TABLE `time_tasks`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `transactions`
--
ALTER TABLE `transactions`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `violations`
--
ALTER TABLE `violations`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `violation_categories`
--
ALTER TABLE `violation_categories`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `violation_category_tiers`
--
ALTER TABLE `violation_category_tiers`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `violation_counter_resets`
--
ALTER TABLE `violation_counter_resets`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `violation_remarks`
--
ALTER TABLE `violation_remarks`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- AUTO_INCREMENT for table `vouchers`
--
ALTER TABLE `vouchers`
  MODIFY `id` int(10) UNSIGNED NOT NULL AUTO_INCREMENT;

--
-- Constraints for dumped tables
--

--
-- Constraints for table `annotation_comments`
--
ALTER TABLE `annotation_comments`
  ADD CONSTRAINT `fk_ac_ann` FOREIGN KEY (`annotation_id`) REFERENCES `image_annotations` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `annotation_images`
--
ALTER TABLE `annotation_images`
  ADD CONSTRAINT `fk_ai_loc` FOREIGN KEY (`location_id`) REFERENCES `locations` (`location_id`);

--
-- Constraints for table `attendance_logs`
--
ALTER TABLE `attendance_logs`
  ADD CONSTRAINT `fk_attendance_location` FOREIGN KEY (`location_id`) REFERENCES `locations` (`location_id`);

--
-- Constraints for table `audits`
--
ALTER TABLE `audits`
  ADD CONSTRAINT `audits_ibfk_1` FOREIGN KEY (`template_id`) REFERENCES `audit_templates` (`id`),
  ADD CONSTRAINT `audits_ibfk_2` FOREIGN KEY (`location_id`) REFERENCES `locations` (`location_id`);

--
-- Constraints for table `audit_categories`
--
ALTER TABLE `audit_categories`
  ADD CONSTRAINT `audit_categories_ibfk_1` FOREIGN KEY (`template_id`) REFERENCES `audit_templates` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `audit_category_weights`
--
ALTER TABLE `audit_category_weights`
  ADD CONSTRAINT `audit_category_weights_ibfk_1` FOREIGN KEY (`audit_id`) REFERENCES `audits` (`id`) ON DELETE CASCADE,
  ADD CONSTRAINT `audit_category_weights_ibfk_2` FOREIGN KEY (`category_id`) REFERENCES `audit_categories` (`id`);

--
-- Constraints for table `audit_history`
--
ALTER TABLE `audit_history`
  ADD CONSTRAINT `audit_history_ibfk_1` FOREIGN KEY (`audit_id`) REFERENCES `audits` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `audit_image_pin_comments`
--
ALTER TABLE `audit_image_pin_comments`
  ADD CONSTRAINT `fk_audit_pin_comment_pin` FOREIGN KEY (`pin_id`) REFERENCES `audit_image_pins` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `audit_parameters`
--
ALTER TABLE `audit_parameters`
  ADD CONSTRAINT `audit_parameters_ibfk_1` FOREIGN KEY (`category_id`) REFERENCES `audit_categories` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `audit_responses`
--
ALTER TABLE `audit_responses`
  ADD CONSTRAINT `audit_responses_ibfk_1` FOREIGN KEY (`audit_id`) REFERENCES `audits` (`id`) ON DELETE CASCADE,
  ADD CONSTRAINT `audit_responses_ibfk_2` FOREIGN KEY (`parameter_id`) REFERENCES `audit_parameters` (`id`);

--
-- Constraints for table `audit_response_attachments`
--
ALTER TABLE `audit_response_attachments`
  ADD CONSTRAINT `audit_response_attachments_ibfk_1` FOREIGN KEY (`response_id`) REFERENCES `audit_responses` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `chk_daily_responses`
--
ALTER TABLE `chk_daily_responses`
  ADD CONSTRAINT `chk_daily_responses_ibfk_1` FOREIGN KEY (`location_id`) REFERENCES `locations` (`location_id`),
  ADD CONSTRAINT `chk_daily_responses_ibfk_2` FOREIGN KEY (`item_id`) REFERENCES `chk_items` (`id`);

--
-- Constraints for table `chk_response_attachments`
--
ALTER TABLE `chk_response_attachments`
  ADD CONSTRAINT `fk_chk_att_response` FOREIGN KEY (`response_id`) REFERENCES `chk_daily_responses` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `devices`
--
ALTER TABLE `devices`
  ADD CONSTRAINT `fk_device_location` FOREIGN KEY (`location_id`) REFERENCES `locations` (`location_id`) ON UPDATE CASCADE;

--
-- Constraints for table `image_annotations`
--
ALTER TABLE `image_annotations`
  ADD CONSTRAINT `fk_ann_img` FOREIGN KEY (`image_id`) REFERENCES `annotation_images` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `issues`
--
ALTER TABLE `issues`
  ADD CONSTRAINT `issues_ibfk_1` FOREIGN KEY (`category_id`) REFERENCES `issue_categories` (`id`),
  ADD CONSTRAINT `issues_ibfk_2` FOREIGN KEY (`location_id`) REFERENCES `locations` (`location_id`);

--
-- Constraints for table `issue_attachments`
--
ALTER TABLE `issue_attachments`
  ADD CONSTRAINT `issue_attachments_ibfk_1` FOREIGN KEY (`issue_id`) REFERENCES `issues` (`id`) ON DELETE CASCADE,
  ADD CONSTRAINT `issue_attachments_ibfk_2` FOREIGN KEY (`comment_id`) REFERENCES `issue_comments` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `issue_category_roles`
--
ALTER TABLE `issue_category_roles`
  ADD CONSTRAINT `issue_category_roles_ibfk_1` FOREIGN KEY (`category_id`) REFERENCES `issue_categories` (`id`) ON DELETE CASCADE,
  ADD CONSTRAINT `issue_category_roles_ibfk_2` FOREIGN KEY (`department_id`) REFERENCES `departments` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `issue_comments`
--
ALTER TABLE `issue_comments`
  ADD CONSTRAINT `issue_comments_ibfk_1` FOREIGN KEY (`issue_id`) REFERENCES `issues` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `issue_status_logs`
--
ALTER TABLE `issue_status_logs`
  ADD CONSTRAINT `issue_status_logs_ibfk_1` FOREIGN KEY (`issue_id`) REFERENCES `issues` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `policy_audience`
--
ALTER TABLE `policy_audience`
  ADD CONSTRAINT `fk_pa_version` FOREIGN KEY (`policy_version_id`) REFERENCES `policy_versions` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `policy_consents`
--
ALTER TABLE `policy_consents`
  ADD CONSTRAINT `fk_pc_employee` FOREIGN KEY (`employee_code`) REFERENCES `employees` (`employee_code`),
  ADD CONSTRAINT `fk_pc_version` FOREIGN KEY (`policy_version_id`) REFERENCES `policy_versions` (`id`);

--
-- Constraints for table `policy_versions`
--
ALTER TABLE `policy_versions`
  ADD CONSTRAINT `fk_pv_policy` FOREIGN KEY (`policy_id`) REFERENCES `policies` (`id`) ON DELETE CASCADE;

--
-- Constraints for table `punch_requests`
--
ALTER TABLE `punch_requests`
  ADD CONSTRAINT `punch_requests_ibfk_1` FOREIGN KEY (`location_id`) REFERENCES `locations` (`location_id`);

--
-- Constraints for table `transactions`
--
ALTER TABLE `transactions`
  ADD CONSTRAINT `fk_txn_location` FOREIGN KEY (`location_id`) REFERENCES `locations` (`location_id`);
COMMIT;

/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
