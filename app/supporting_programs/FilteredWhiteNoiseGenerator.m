
sample_rate = 96000;
file_duration_s = 5*60;
num_samples = sample_rate * file_duration_s;
L_freq = 500;
R_freq = 3000;

white_noise_gen = dsp.ColoredNoise('Color', 'white', ...
                                'SamplesPerFrame', sample_rate*file_duration_s,...
                                'NumChannels', 2, ...
                                'BoundedOutput', true);
white_noise_reg = white_noise_gen();
white_noise = int32(white_noise_reg .* (2^31-1));

white_noise_transpose = white_noise';
white_noise_array = white_noise_transpose(:);

t = linspace(0, file_duration_s, num_samples);
sin_gen_L = int32(sin(2*pi*L_freq*t).* (2^31-1));
sin_gen_R = int32(sin(2*pi*R_freq*t).* (2^31-1));

sin_audio = [sin_gen_L' sin_gen_R'];
sin_audio_t = sin_audio';
sin_array = sin_audio_t(:);
audiowrite("SINEA.wav",...
            sin_audio, sample_rate, 'BitsPerSample',32);

filename = 'sineWaveAudioData.xlsx';
writematrix(sin_array,filename,'Sheet',1,'Range','B1:');

%plot(t, sin_array);


%audiowrite("E:\RUNAUDIO.wav",...
%            white_noise, sample_rate, 'BitsPerSample',32);


%    N = 4; % Filter order
%    gain = -10; % Gain in dB
%    centerFreq = 0.5; % Normalized center frequency (0 to 1)
%    bandwidth = 0.1; % Normalized bandwidth
% 
%    % Design the parametric equalizer
%    [B, A] = designParamEQ(N, gain, centerFreq, bandwidth);
% 
% 
% % Filter the white noise
%    filtered_noise = filter(B, A, white_noise);
% 
% % Plot the original and filtered noise
%    figure;
%    subplot(2, 1, 1);
%    plot(white_noise);
%    title('Original White Noise');
%    subplot(2, 1, 2);
%    plot(filtered_noise);
%    title('Filtered Noise');

%sound(white_noise, sample_rate);